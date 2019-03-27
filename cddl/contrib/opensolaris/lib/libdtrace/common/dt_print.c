/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2011 by Delphix. All rights reserved.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

/*
 * DTrace print() action
 *
 * This file contains the post-processing logic for the print() action.  The
 * print action behaves identically to trace() in that it generates a
 * DTRACEACT_DIFEXPR action, but the action argument field refers to a CTF type
 * string stored in the DOF string table (similar to printf formats).  We
 * take the result of the trace action and post-process it in the fashion of
 * MDB's ::print dcmd.
 *
 * This implementation differs from MDB's in the following ways:
 *
 * 	- We do not expose any options or flags.  The behavior of print() is
 *	  equivalent to "::print -tn".
 *
 * 	- MDB will display "holes" in structures (unused padding between
 *	  members).
 *
 * 	- When printing arrays of structures, MDB will leave a trailing ','
 *	  after the last element.
 *
 *	- MDB will print time_t types as date and time.
 *
 *	- MDB will detect when an enum is actually the OR of several flags,
 *	  and print it out with the constituent flags separated.
 *
 *	- For large arrays, MDB will print the first few members and then
 *	  print a "..." continuation line.
 *
 *	- MDB will break and wrap arrays at 80 columns.
 *
 *	- MDB prints out floats and doubles by hand, as it must run in kmdb
 *	  context.  We're able to leverage the printf() format strings,
 *	  but the result is a slightly different format.
 */

#include <sys/sysmacros.h>
#include <strings.h>
#include <stdlib.h>
#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <dt_module.h>
#include <dt_printf.h>
#include <dt_string.h>
#include <dt_impl.h>

/* determines whether the given integer CTF encoding is a character */
#define	CTF_IS_CHAR(e) \
	(((e).cte_format & (CTF_INT_CHAR | CTF_INT_SIGNED)) == \
	(CTF_INT_CHAR | CTF_INT_SIGNED) && (e).cte_bits == NBBY)
/* determines whether the given CTF kind is a struct or union */
#define	CTF_IS_STRUCTLIKE(k) \
	((k) == CTF_K_STRUCT || (k) == CTF_K_UNION)

/*
 * Print structure passed down recursively through printing algorithm.
 */
typedef struct dt_printarg {
	dtrace_hdl_t	*pa_dtp;	/* libdtrace handle */
	caddr_t		pa_addr;	/* base address of trace data */
	ctf_file_t	*pa_ctfp;	/* CTF container */
	int		pa_depth;	/* member depth */
	int		pa_nest;	/* nested array depth */
	FILE		*pa_file;	/* output file */
} dt_printarg_t;

static int dt_print_member(const char *, ctf_id_t, ulong_t, int, void *);

/*
 * Safe version of ctf_type_name() that will fall back to just "<ctfid>" if it
 * can't resolve the type.
 */
static void
dt_print_type_name(ctf_file_t *ctfp, ctf_id_t id, char *buf, size_t buflen)
{
	if (ctf_type_name(ctfp, id, buf, buflen) == NULL)
		(void) snprintf(buf, buflen, "<%ld>", id);
}

/*
 * Print any necessary trailing braces for structures or unions.  We don't get
 * invoked when a struct or union ends, so we infer the need to print braces
 * based on the depth the last time we printed something and the new depth.
 */
static void
dt_print_trailing_braces(dt_printarg_t *pap, int depth)
{
	int d;

	for (d = pap->pa_depth; d > depth; d--) {
		(void) fprintf(pap->pa_file, "%*s}%s",
		    (d + pap->pa_nest - 1) * 4, "",
		    d == depth + 1 ? "" : "\n");
	}
}

/*
 * Print the appropriate amount of indentation given the current depth and
 * array nesting.
 */
static void
dt_print_indent(dt_printarg_t *pap)
{
	(void) fprintf(pap->pa_file, "%*s",
	    (pap->pa_depth + pap->pa_nest) * 4, "");
}

/*
 * Print a bitfield.  It's worth noting that the D compiler support for
 * bitfields is currently broken; printing "D`user_desc_t" (pulled in by the
 * various D provider files) will produce incorrect results compared to
 * "genunix`user_desc_t".
 */
static void
print_bitfield(dt_printarg_t *pap, ulong_t off, ctf_encoding_t *ep)
{
	FILE *fp = pap->pa_file;
	caddr_t addr = pap->pa_addr + off / NBBY;
	uint64_t mask = (1ULL << ep->cte_bits) - 1;
	uint64_t value = 0;
	size_t size = (ep->cte_bits + (NBBY - 1)) / NBBY;
	uint8_t *buf = (uint8_t *)&value;
	uint8_t shift;

	/*
	 * On big-endian machines, we need to adjust the buf pointer to refer
	 * to the lowest 'size' bytes in 'value', and we need to shift based on
	 * the offset from the end of the data, not the offset of the start.
	 */
#if BYTE_ORDER == _BIG_ENDIAN
	buf += sizeof (value) - size;
	off += ep->cte_bits;
#endif
	bcopy(addr, buf, size);
	shift = off % NBBY;

	/*
	 * Offsets are counted from opposite ends on little- and
	 * big-endian machines.
	 */
#if BYTE_ORDER == _BIG_ENDIAN
	shift = NBBY - shift;
#endif

	/*
	 * If the bits we want do not begin on a byte boundary, shift the data
	 * right so that the value is in the lowest 'cte_bits' of 'value'.
	 */
	if (off % NBBY != 0)
		value >>= shift;
	value &= mask;

	(void) fprintf(fp, "%#llx", (u_longlong_t)value);
}

/*
 * Dump the contents of memory as a fixed-size integer in hex.
 */
static void
dt_print_hex(FILE *fp, caddr_t addr, size_t size)
{
	switch (size) {
	case sizeof (uint8_t):
		(void) fprintf(fp, "%#x", *(uint8_t *)addr);
		break;
	case sizeof (uint16_t):
		/* LINTED - alignment */
		(void) fprintf(fp, "%#x", *(uint16_t *)addr);
		break;
	case sizeof (uint32_t):
		/* LINTED - alignment */
		(void) fprintf(fp, "%#x", *(uint32_t *)addr);
		break;
	case sizeof (uint64_t):
		(void) fprintf(fp, "%#llx",
		    /* LINTED - alignment */
		    (unsigned long long)*(uint64_t *)addr);
		break;
	default:
		(void) fprintf(fp, "<invalid size %u>", (uint_t)size);
	}
}

/*
 * Print an integer type.  Before dumping the contents via dt_print_hex(), we
 * first check the encoding to see if it's part of a bitfield or a character.
 */
static void
dt_print_int(ctf_id_t base, ulong_t off, dt_printarg_t *pap)
{
	FILE *fp = pap->pa_file;
	ctf_file_t *ctfp = pap->pa_ctfp;
	ctf_encoding_t e;
	size_t size;
	caddr_t addr = pap->pa_addr + off / NBBY;

	if (ctf_type_encoding(ctfp, base, &e) == CTF_ERR) {
		(void) fprintf(fp, "<unknown encoding>");
		return;
	}

	/*
	 * This comes from MDB - it's not clear under what circumstances this
	 * would be found.
	 */
	if (e.cte_format & CTF_INT_VARARGS) {
		(void) fprintf(fp, "...");
		return;
	}

	/*
	 * We print this as a bitfield if the bit encoding indicates it's not
	 * an even power of two byte size, or is larger than 8 bytes.
	 */
	size = e.cte_bits / NBBY;
	if (size > 8 || (e.cte_bits % NBBY) != 0 || (size & (size - 1)) != 0) {
		print_bitfield(pap, off, &e);
		return;
	}

	/*
	 * If this is a character, print it out as such.
	 */
	if (CTF_IS_CHAR(e)) {
		char c = *(char *)addr;
		if (isprint(c))
			(void) fprintf(fp, "'%c'", c);
		else if (c == 0)
			(void) fprintf(fp, "'\\0'");
		else
			(void) fprintf(fp, "'\\%03o'", c);
		return;
	}

	dt_print_hex(fp, addr, size);
}

/*
 * Print a floating point (float, double, long double) value.
 */
/* ARGSUSED */
static void
dt_print_float(ctf_id_t base, ulong_t off, dt_printarg_t *pap)
{
	FILE *fp = pap->pa_file;
	ctf_file_t *ctfp = pap->pa_ctfp;
	ctf_encoding_t e;
	caddr_t addr = pap->pa_addr + off / NBBY;

	if (ctf_type_encoding(ctfp, base, &e) == 0) {
		if (e.cte_format == CTF_FP_SINGLE &&
		    e.cte_bits == sizeof (float) * NBBY) {
			/* LINTED - alignment */
			(void) fprintf(fp, "%+.7e", *((float *)addr));
		} else if (e.cte_format == CTF_FP_DOUBLE &&
		    e.cte_bits == sizeof (double) * NBBY) {
			/* LINTED - alignment */
			(void) fprintf(fp, "%+.7e", *((double *)addr));
		} else if (e.cte_format == CTF_FP_LDOUBLE &&
		    e.cte_bits == sizeof (long double) * NBBY) {
			/* LINTED - alignment */
			(void) fprintf(fp, "%+.16LE", *((long double *)addr));
		} else {
			(void) fprintf(fp, "<unknown encoding>");
		}
	}
}

/*
 * A pointer is generally printed as a fixed-size integer.  If we have a
 * function pointer, we try to look up its name.
 */
static void
dt_print_ptr(ctf_id_t base, ulong_t off, dt_printarg_t *pap)
{
	FILE *fp = pap->pa_file;
	ctf_file_t *ctfp = pap->pa_ctfp;
	caddr_t addr = pap->pa_addr + off / NBBY;
	size_t size = ctf_type_size(ctfp, base);
	ctf_id_t bid = ctf_type_reference(ctfp, base);
	uint64_t pc;
	dtrace_syminfo_t dts;
	GElf_Sym sym;

	if (bid == CTF_ERR || ctf_type_kind(ctfp, bid) != CTF_K_FUNCTION) {
		dt_print_hex(fp, addr, size);
	} else {
		/* LINTED - alignment */
		pc = *((uint64_t *)addr);
		if (dtrace_lookup_by_addr(pap->pa_dtp, pc, &sym, &dts) != 0) {
			dt_print_hex(fp, addr, size);
		} else {
			(void) fprintf(fp, "%s`%s", dts.dts_object,
			    dts.dts_name);
		}
	}
}

/*
 * Print out an array.  This is somewhat complex, as we must manually visit
 * each member, and recursively invoke ctf_type_visit() for each member.  If
 * the members are non-structs, then we print them out directly:
 *
 * 	[ 0x14, 0x2e, 0 ]
 *
 * If they are structs, then we print out the necessary leading and trailing
 * braces, to end up with:
 *
 *	[
 *	    type {
 *	    ...
 *	    },
 *	    type {
 *	    ...
 *	    }
 *	]
 *
 * We also use a heuristic to detect whether the array looks like a character
 * array.  If the encoding indicates it's a character, and we have all
 * printable characters followed by a null byte, then we display it as a
 * string:
 *
 *	[ "string" ]
 */
static void
dt_print_array(ctf_id_t base, ulong_t off, dt_printarg_t *pap)
{
	FILE *fp = pap->pa_file;
	ctf_file_t *ctfp = pap->pa_ctfp;
	caddr_t addr = pap->pa_addr + off / NBBY;
	ctf_arinfo_t car;
	ssize_t eltsize;
	ctf_encoding_t e;
	int i;
	boolean_t isstring;
	int kind;
	ctf_id_t rtype;

	if (ctf_array_info(ctfp, base, &car) == CTF_ERR) {
		(void) fprintf(fp, "%p", (void *)addr);
		return;
	}

	if ((eltsize = ctf_type_size(ctfp, car.ctr_contents)) < 0 ||
	    (rtype = ctf_type_resolve(ctfp, car.ctr_contents)) == CTF_ERR ||
	    (kind = ctf_type_kind(ctfp, rtype)) == CTF_ERR) {
		(void) fprintf(fp, "<invalid type %lu>", car.ctr_contents);
		return;
	}

	/* see if this looks like a string */
	isstring = B_FALSE;
	if (kind == CTF_K_INTEGER &&
	    ctf_type_encoding(ctfp, rtype, &e) != CTF_ERR && CTF_IS_CHAR(e)) {
		char c;
		for (i = 0; i < car.ctr_nelems; i++) {
			c = *((char *)addr + eltsize * i);
			if (!isprint(c) || c == '\0')
				break;
		}

		if (i != car.ctr_nelems && c == '\0')
			isstring = B_TRUE;
	}

	/*
	 * As a slight aesthetic optimization, if we are a top-level type, then
	 * don't bother printing out the brackets.  This lets print("foo") look
	 * like:
	 *
	 * 	string "foo"
	 *
	 * As D will internally represent this as a char[256] array.
	 */
	if (!isstring || pap->pa_depth != 0)
		(void) fprintf(fp, "[ ");

	if (isstring)
		(void) fprintf(fp, "\"");

	for (i = 0; i < car.ctr_nelems; i++) {
		if (isstring) {
			char c = *((char *)addr + eltsize * i);
			if (c == '\0')
				break;
			(void) fprintf(fp, "%c", c);
		} else {
			/*
			 * Recursively invoke ctf_type_visit() on each member.
			 * We setup a new printarg struct with 'pa_nest' set to
			 * indicate that we are within a nested array.
			 */
			dt_printarg_t pa = *pap;
			pa.pa_nest += pap->pa_depth + 1;
			pa.pa_depth = 0;
			pa.pa_addr = addr + eltsize * i;
			(void) ctf_type_visit(ctfp, car.ctr_contents,
			    dt_print_member, &pa);

			dt_print_trailing_braces(&pa, 0);
			if (i != car.ctr_nelems - 1)
				(void) fprintf(fp, ", ");
			else if (CTF_IS_STRUCTLIKE(kind))
				(void) fprintf(fp, "\n");
		}
	}

	if (isstring)
		(void) fprintf(fp, "\"");

	if (!isstring || pap->pa_depth != 0) {
		if (CTF_IS_STRUCTLIKE(kind))
			dt_print_indent(pap);
		else
			(void) fprintf(fp, " ");
		(void) fprintf(fp, "]");
	}
}

/*
 * This isued by both structs and unions to print the leading brace.
 */
/* ARGSUSED */
static void
dt_print_structlike(ctf_id_t id, ulong_t off, dt_printarg_t *pap)
{
	(void) fprintf(pap->pa_file, "{");
}

/*
 * For enums, we try to print the enum name, and fall back to the value if it
 * can't be determined.  We do not do any fancy flag processing like mdb.
 */
/* ARGSUSED */
static void
dt_print_enum(ctf_id_t base, ulong_t off, dt_printarg_t *pap)
{
	FILE *fp = pap->pa_file;
	ctf_file_t *ctfp = pap->pa_ctfp;
	const char *ename;
	ssize_t size;
	caddr_t addr = pap->pa_addr + off / NBBY;
	int value = 0;

	/*
	 * The C standard says that an enum will be at most the sizeof (int).
	 * But if all the values are less than that, the compiler can use a
	 * smaller size. Thanks standards.
	 */
	size = ctf_type_size(ctfp, base);
	switch (size) {
	case sizeof (uint8_t):
		value = *(uint8_t *)addr;
		break;
	case sizeof (uint16_t):
		value = *(uint16_t *)addr;
		break;
	case sizeof (int32_t):
		value = *(int32_t *)addr;
		break;
	default:
		(void) fprintf(fp, "<invalid enum size %u>", (uint_t)size);
		return;
	}

	if ((ename = ctf_enum_name(ctfp, base, value)) != NULL)
		(void) fprintf(fp, "%s", ename);
	else
		(void) fprintf(fp, "%d", value);
}

/*
 * Forward declaration.  There's not much to do here without the complete
 * type information, so just print out this fact and drive on.
 */
/* ARGSUSED */
static void
dt_print_tag(ctf_id_t base, ulong_t off, dt_printarg_t *pap)
{
	(void) fprintf(pap->pa_file, "<forward decl>");
}

typedef void dt_printarg_f(ctf_id_t, ulong_t, dt_printarg_t *);

static dt_printarg_f *const dt_printfuncs[] = {
	dt_print_int,		/* CTF_K_INTEGER */
	dt_print_float,		/* CTF_K_FLOAT */
	dt_print_ptr,		/* CTF_K_POINTER */
	dt_print_array,		/* CTF_K_ARRAY */
	dt_print_ptr,		/* CTF_K_FUNCTION */
	dt_print_structlike,	/* CTF_K_STRUCT */
	dt_print_structlike,	/* CTF_K_UNION */
	dt_print_enum,		/* CTF_K_ENUM */
	dt_print_tag		/* CTF_K_FORWARD */
};

/*
 * Print one member of a structure.  This callback is invoked from
 * ctf_type_visit() recursively.
 */
static int
dt_print_member(const char *name, ctf_id_t id, ulong_t off, int depth,
    void *data)
{
	char type[DT_TYPE_NAMELEN];
	int kind;
	dt_printarg_t *pap = data;
	FILE *fp = pap->pa_file;
	ctf_file_t *ctfp = pap->pa_ctfp;
	boolean_t arraymember;
	boolean_t brief;
	ctf_encoding_t e;
	ctf_id_t rtype;

	dt_print_trailing_braces(pap, depth);
	/*
	 * dt_print_trailing_braces() doesn't include the trailing newline; add
	 * it here if necessary.
	 */
	if (depth < pap->pa_depth)
		(void) fprintf(fp, "\n");
	pap->pa_depth = depth;

	if ((rtype = ctf_type_resolve(ctfp, id)) == CTF_ERR ||
	    (kind = ctf_type_kind(ctfp, rtype)) == CTF_ERR ||
	    kind < CTF_K_INTEGER || kind > CTF_K_FORWARD) {
		dt_print_indent(pap);
		(void) fprintf(fp, "%s = <invalid type %lu>", name, id);
		return (0);
	}

	dt_print_type_name(ctfp, id, type, sizeof (type));

	arraymember = (pap->pa_nest != 0 && depth == 0);
	brief = (arraymember && !CTF_IS_STRUCTLIKE(kind));

	if (!brief) {
		/*
		 * If this is a direct array member and a struct (otherwise
		 * brief would be true), then print a trailing newline, as the
		 * array printing code doesn't include it because it might be a
		 * simple type.
		 */
		if (arraymember)
			(void) fprintf(fp, "\n");
		dt_print_indent(pap);

		/* always print the type */
		(void) fprintf(fp, "%s", type);
		if (name[0] != '\0') {
			/*
			 * For aesthetics, we don't include a space between the
			 * type name and member name if the type is a pointer.
			 * This will give us "void *foo =" instead of "void *
			 * foo =".  Unions also have the odd behavior that the
			 * type name is returned as "union ", with a trailing
			 * space, so we also avoid printing a space if the type
			 * name already ends with a space.
			 */
			if (type[strlen(type) - 1] != '*' &&
			    type[strlen(type) -1] != ' ') {
				(void) fprintf(fp, " ");
			}
			(void) fprintf(fp, "%s", name);

			/*
			 * If this looks like a bitfield, or is an integer not
			 * aligned on a byte boundary, print the number of
			 * bits after the name.
			 */
			if (kind == CTF_K_INTEGER &&
			    ctf_type_encoding(ctfp, id, &e) == 0) {
				ulong_t bits = e.cte_bits;
				ulong_t size = bits / NBBY;

				if (bits % NBBY != 0 ||
				    off % NBBY != 0 ||
				    size > 8 ||
				    size != ctf_type_size(ctfp, id)) {
					(void) fprintf(fp, " :%lu", bits);
				}
			}

			(void) fprintf(fp, " =");
		}
		(void) fprintf(fp, " ");
	}

	dt_printfuncs[kind - 1](rtype, off, pap);

	/* direct simple array members are not separated by newlines */
	if (!brief)
		(void) fprintf(fp, "\n");

	return (0);
}

/*
 * Main print function invoked by dt_consume_cpu().
 */
int
dtrace_print(dtrace_hdl_t *dtp, FILE *fp, const char *typename,
    caddr_t addr, size_t len)
{
	const char *s;
	char *object;
	dt_printarg_t pa;
	ctf_id_t id;
	dt_module_t *dmp;
	ctf_file_t *ctfp;
	int libid;

	/*
	 * Split the fully-qualified type ID (module`id).  This should
	 * always be the format, but if for some reason we don't find the
	 * expected value, return 0 to fall back to the generic trace()
	 * behavior. In the case of userland CTF modules this will actually be
	 * of the format (module`lib`id). This is due to the fact that those
	 * modules have multiple CTF containers which `lib` identifies.
	 */
	for (s = typename; *s != '\0' && *s != '`'; s++)
		;

	if (*s != '`')
		return (0);

	object = alloca(s - typename + 1);
	bcopy(typename, object, s - typename);
	object[s - typename] = '\0';
	dmp = dt_module_lookup_by_name(dtp, object);
	if (dmp == NULL)
		return (0);

	if (dmp->dm_pid != 0) {
		libid = atoi(s + 1);
		s = strchr(s + 1, '`');
		if (s == NULL || libid > dmp->dm_nctflibs)
			return (0);
		ctfp = dmp->dm_libctfp[libid];
	} else {
		ctfp = dt_module_getctf(dtp, dmp);
	}

	id = atoi(s + 1);

	/*
	 * Try to get the CTF kind for this id.  If something has gone horribly
	 * wrong and we can't resolve the ID, bail out and let trace() do the
	 * work.
	 */
	if (ctfp == NULL || ctf_type_kind(ctfp, id) == CTF_ERR)
		return (0);

	/* setup the print structure and kick off the main print routine */
	pa.pa_dtp = dtp;
	pa.pa_addr = addr;
	pa.pa_ctfp = ctfp;
	pa.pa_nest = 0;
	pa.pa_depth = 0;
	pa.pa_file = fp;
	(void) ctf_type_visit(pa.pa_ctfp, id, dt_print_member, &pa);

	dt_print_trailing_braces(&pa, 0);

	return (len);
}
