/*
 * altera.c
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010,2011 NetUP Inc.
 * Copyright (C) 2010,2011 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/unaligned.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <staging/altera.h>
#include "altera-exprt.h"
#include "altera-jtag.h"

static int debug = 1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debugging information");

MODULE_DESCRIPTION("altera FPGA kernel module");
MODULE_AUTHOR("Igor M. Liplianin  <liplianin@netup.ru>");
MODULE_LICENSE("GPL");

#define dprintk(args...) \
	if (debug) { \
		printk(KERN_DEBUG args); \
	}

enum altera_fpga_opcode {
	OP_NOP = 0,
	OP_DUP,
	OP_SWP,
	OP_ADD,
	OP_SUB,
	OP_MULT,
	OP_DIV,
	OP_MOD,
	OP_SHL,
	OP_SHR,
	OP_NOT,
	OP_AND,
	OP_OR,
	OP_XOR,
	OP_INV,
	OP_GT,
	OP_LT,
	OP_RET,
	OP_CMPS,
	OP_PINT,
	OP_PRNT,
	OP_DSS,
	OP_DSSC,
	OP_ISS,
	OP_ISSC,
	OP_DPR = 0x1c,
	OP_DPRL,
	OP_DPO,
	OP_DPOL,
	OP_IPR,
	OP_IPRL,
	OP_IPO,
	OP_IPOL,
	OP_PCHR,
	OP_EXIT,
	OP_EQU,
	OP_POPT,
	OP_ABS = 0x2c,
	OP_BCH0,
	OP_PSH0 = 0x2f,
	OP_PSHL = 0x40,
	OP_PSHV,
	OP_JMP,
	OP_CALL,
	OP_NEXT,
	OP_PSTR,
	OP_SINT = 0x47,
	OP_ST,
	OP_ISTP,
	OP_DSTP,
	OP_SWPN,
	OP_DUPN,
	OP_POPV,
	OP_POPE,
	OP_POPA,
	OP_JMPZ,
	OP_DS,
	OP_IS,
	OP_DPRA,
	OP_DPOA,
	OP_IPRA,
	OP_IPOA,
	OP_EXPT,
	OP_PSHE,
	OP_PSHA,
	OP_DYNA,
	OP_EXPV = 0x5c,
	OP_COPY = 0x80,
	OP_REVA,
	OP_DSC,
	OP_ISC,
	OP_WAIT,
	OP_VS,
	OP_CMPA = 0xc0,
	OP_VSC,
};

struct altera_procinfo {
	char			*name;
	u8			attrs;
	struct altera_procinfo	*next;
};

/* This function checks if enough parameters are available on the stack. */
static int altera_check_stack(int stack_ptr, int count, int *status)
{
	if (stack_ptr < count) {
		*status = -EOVERFLOW;
		return 0;
	}

	return 1;
}

static void altera_export_int(char *key, s32 value)
{
	dprintk("Export: key = \"%s\", value = %d\n", key, value);
}

#define HEX_LINE_CHARS 72
#define HEX_LINE_BITS (HEX_LINE_CHARS * 4)

static void altera_export_bool_array(char *key, u8 *data, s32 count)
{
	char string[HEX_LINE_CHARS + 1];
	s32 i, offset;
	u32 size, line, lines, linebits, value, j, k;

	if (count > HEX_LINE_BITS) {
		dprintk("Export: key = \"%s\", %d bits, value = HEX\n",
							key, count);
		lines = (count + (HEX_LINE_BITS - 1)) / HEX_LINE_BITS;

		for (line = 0; line < lines; ++line) {
			if (line < (lines - 1)) {
				linebits = HEX_LINE_BITS;
				size = HEX_LINE_CHARS;
				offset = count - ((line + 1) * HEX_LINE_BITS);
			} else {
				linebits =
					count - ((lines - 1) * HEX_LINE_BITS);
				size = (linebits + 3) / 4;
				offset = 0L;
			}

			string[size] = '\0';
			j = size - 1;
			value = 0;

			for (k = 0; k < linebits; ++k) {
				i = k + offset;
				if (data[i >> 3] & (1 << (i & 7)))
					value |= (1 << (i & 3));
				if ((i & 3) == 3) {
					sprintf(&string[j], "%1x", value);
					value = 0;
					--j;
				}
			}
			if ((k & 3) > 0)
				sprintf(&string[j], "%1x", value);

			dprintk("%s\n", string);
		}

	} else {
		size = (count + 3) / 4;
		string[size] = '\0';
		j = size - 1;
		value = 0;

		for (i = 0; i < count; ++i) {
			if (data[i >> 3] & (1 << (i & 7)))
				value |= (1 << (i & 3));
			if ((i & 3) == 3) {
				sprintf(&string[j], "%1x", value);
				value = 0;
				--j;
			}
		}
		if ((i & 3) > 0)
			sprintf(&string[j], "%1x", value);

		dprintk("Export: key = \"%s\", %d bits, value = HEX %s\n",
			key, count, string);
	}
}

static int altera_execute(struct altera_state *astate,
				u8 *p,
				s32 program_size,
				s32 *error_address,
				int *exit_code,
				int *format_version)
{
	struct altera_config *aconf = astate->config;
	char *msg_buff = astate->msg_buff;
	long *stack = astate->stack;
	int status = 0;
	u32 first_word = 0L;
	u32 action_table = 0L;
	u32 proc_table = 0L;
	u32 str_table = 0L;
	u32 sym_table = 0L;
	u32 data_sect = 0L;
	u32 code_sect = 0L;
	u32 debug_sect = 0L;
	u32 action_count = 0L;
	u32 proc_count = 0L;
	u32 sym_count = 0L;
	long *vars = NULL;
	s32 *var_size = NULL;
	char *attrs = NULL;
	u8 *proc_attributes = NULL;
	u32 pc;
	u32 opcode_address;
	u32 args[3];
	u32 opcode;
	u32 name_id;
	u8 charbuf[4];
	long long_tmp;
	u32 variable_id;
	u8 *charptr_tmp;
	u8 *charptr_tmp2;
	long *longptr_tmp;
	int version = 0;
	int delta = 0;
	int stack_ptr = 0;
	u32 arg_count;
	int done = 0;
	int bad_opcode = 0;
	u32 count;
	u32 index;
	u32 index2;
	s32 long_count;
	s32 long_idx;
	s32 long_idx2;
	u32 i;
	u32 j;
	u32 uncomp_size;
	u32 offset;
	u32 value;
	int current_proc = 0;
	int reverse;

	char *name;

	dprintk("%s\n", __func__);

	/* Read header information */
	if (program_size > 52L) {
		first_word    = get_unaligned_be32(&p[0]);
		version = (first_word & 1L);
		*format_version = version + 1;
		delta = version * 8;

		action_table  = get_unaligned_be32(&p[4]);
		proc_table    = get_unaligned_be32(&p[8]);
		str_table  = get_unaligned_be32(&p[4 + delta]);
		sym_table  = get_unaligned_be32(&p[16 + delta]);
		data_sect  = get_unaligned_be32(&p[20 + delta]);
		code_sect  = get_unaligned_be32(&p[24 + delta]);
		debug_sect = get_unaligned_be32(&p[28 + delta]);
		action_count  = get_unaligned_be32(&p[40 + delta]);
		proc_count    = get_unaligned_be32(&p[44 + delta]);
		sym_count  = get_unaligned_be32(&p[48 + (2 * delta)]);
	}

	if ((first_word != 0x4A414D00L) && (first_word != 0x4A414D01L)) {
		done = 1;
		status = -EIO;
		goto exit_done;
	}

	if (sym_count <= 0)
		goto exit_done;

	vars = kzalloc(sym_count * sizeof(long), GFP_KERNEL);

	if (vars == NULL)
		status = -ENOMEM;

	if (status == 0) {
		var_size = kzalloc(sym_count * sizeof(s32), GFP_KERNEL);

		if (var_size == NULL)
			status = -ENOMEM;
	}

	if (status == 0) {
		attrs = kzalloc(sym_count, GFP_KERNEL);

		if (attrs == NULL)
			status = -ENOMEM;
	}

	if ((status == 0) && (version > 0)) {
		proc_attributes = kzalloc(proc_count, GFP_KERNEL);

		if (proc_attributes == NULL)
			status = -ENOMEM;
	}

	if (status != 0)
		goto exit_done;

	delta = version * 2;

	for (i = 0; i < sym_count; ++i) {
		offset = (sym_table + ((11 + delta) * i));

		value = get_unaligned_be32(&p[offset + 3 + delta]);

		attrs[i] = p[offset];

		/*
		 * use bit 7 of attribute byte to indicate that
		 * this buffer was dynamically allocated
		 * and should be freed later
		 */
		attrs[i] &= 0x7f;

		var_size[i] = get_unaligned_be32(&p[offset + 7 + delta]);

		/*
		 * Attribute bits:
		 * bit 0: 0 = read-only, 1 = read-write
		 * bit 1: 0 = not compressed, 1 = compressed
		 * bit 2: 0 = not initialized, 1 = initialized
		 * bit 3: 0 = scalar, 1 = array
		 * bit 4: 0 = Boolean, 1 = integer
		 * bit 5: 0 = declared variable,
		 *	1 = compiler created temporary variable
		 */

		if ((attrs[i] & 0x0c) == 0x04)
			/* initialized scalar variable */
			vars[i] = value;
		else if ((attrs[i] & 0x1e) == 0x0e) {
			/* initialized compressed Boolean array */
			uncomp_size = get_unaligned_le32(&p[data_sect + value]);

			/* allocate a buffer for the uncompressed data */
			vars[i] = (long)kzalloc(uncomp_size, GFP_KERNEL);
			if (vars[i] == 0L)
				status = -ENOMEM;
			else {
				/* set flag so buffer will be freed later */
				attrs[i] |= 0x80;

				/* uncompress the data */
				if (altera_shrink(&p[data_sect + value],
						var_size[i],
						(u8 *)vars[i],
						uncomp_size,
						version) != uncomp_size)
					/* decompression failed */
					status = -EIO;
				else
					var_size[i] = uncomp_size * 8L;

			}
		} else if ((attrs[i] & 0x1e) == 0x0c) {
			/* initialized Boolean array */
			vars[i] = value + data_sect + (long)p;
		} else if ((attrs[i] & 0x1c) == 0x1c) {
			/* initialized integer array */
			vars[i] = value + data_sect;
		} else if ((attrs[i] & 0x0c) == 0x08) {
			/* uninitialized array */

			/* flag attrs so that memory is freed */
			attrs[i] |= 0x80;

			if (var_size[i] > 0) {
				u32 size;

				if (attrs[i] & 0x10)
					/* integer array */
					size = (var_size[i] * sizeof(s32));
				else
					/* Boolean array */
					size = ((var_size[i] + 7L) / 8L);

				vars[i] = (long)kzalloc(size, GFP_KERNEL);

				if (vars[i] == 0) {
					status = -ENOMEM;
				} else {
					/* zero out memory */
					for (j = 0; j < size; ++j)
						((u8 *)(vars[i]))[j] = 0;

				}
			} else
				vars[i] = 0;

		} else
			vars[i] = 0;

	}

exit_done:
	if (status != 0)
		done = 1;

	altera_jinit(astate);

	pc = code_sect;
	msg_buff[0] = '\0';

	/*
	 * For JBC version 2, we will execute the procedures corresponding to
	 * the selected ACTION
	 */
	if (version > 0) {
		if (aconf->action == NULL) {
			status = -EINVAL;
			done = 1;
		} else {
			int action_found = 0;
			for (i = 0; (i < action_count) && !action_found; ++i) {
				name_id = get_unaligned_be32(&p[action_table +
								(12 * i)]);

				name = &p[str_table + name_id];

				if (strnicmp(aconf->action, name, strlen(name)) == 0) {
					action_found = 1;
					current_proc =
						get_unaligned_be32(&p[action_table +
								(12 * i) + 8]);
				}
			}

			if (!action_found) {
				status = -EINVAL;
				done = 1;
			}
		}

		if (status == 0) {
			int first_time = 1;
			i = current_proc;
			while ((i != 0) || first_time) {
				first_time = 0;
				/* check procedure attribute byte */
				proc_attributes[i] =
						(p[proc_table +
								(13 * i) + 8] &
									0x03);

				/*
				 * BIT0 - OPTIONAL
				 * BIT1 - RECOMMENDED
				 * BIT6 - FORCED OFF
				 * BIT7 - FORCED ON
				 */

				i = get_unaligned_be32(&p[proc_table +
							(13 * i) + 4]);
			}

			/*
			 * Set current_proc to the first procedure
			 * to be executed
			 */
			i = current_proc;
			while ((i != 0) &&
				((proc_attributes[i] == 1) ||
				((proc_attributes[i] & 0xc0) == 0x40))) {
				i = get_unaligned_be32(&p[proc_table +
							(13 * i) + 4]);
			}

			if ((i != 0) || ((i == 0) && (current_proc == 0) &&
				((proc_attributes[0] != 1) &&
				((proc_attributes[0] & 0xc0) != 0x40)))) {
				current_proc = i;
				pc = code_sect +
					get_unaligned_be32(&p[proc_table +
								(13 * i) + 9]);
				if ((pc < code_sect) || (pc >= debug_sect))
					status = -ERANGE;
			} else
				/* there are no procedures to execute! */
				done = 1;

		}
	}

	msg_buff[0] = '\0';

	while (!done) {
		opcode = (p[pc] & 0xff);
		opcode_address = pc;
		++pc;

		if (debug > 1)
			printk("opcode: %02x\n", opcode);

		arg_count = (opcode >> 6) & 3;
		for (i = 0; i < arg_count; ++i) {
			args[i] = get_unaligned_be32(&p[pc]);
			pc += 4;
		}

		switch (opcode) {
		case OP_NOP:
			break;
		case OP_DUP:
			if (altera_check_stack(stack_ptr, 1, &status)) {
				stack[stack_ptr] = stack[stack_ptr - 1];
				++stack_ptr;
			}
			break;
		case OP_SWP:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				long_tmp = stack[stack_ptr - 2];
				stack[stack_ptr - 2] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}
			break;
		case OP_ADD:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] += stack[stack_ptr];
			}
			break;
		case OP_SUB:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] -= stack[stack_ptr];
			}
			break;
		case OP_MULT:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] *= stack[stack_ptr];
			}
			break;
		case OP_DIV:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] /= stack[stack_ptr];
			}
			break;
		case OP_MOD:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] %= stack[stack_ptr];
			}
			break;
		case OP_SHL:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] <<= stack[stack_ptr];
			}
			break;
		case OP_SHR:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] >>= stack[stack_ptr];
			}
			break;
		case OP_NOT:
			if (altera_check_stack(stack_ptr, 1, &status))
				stack[stack_ptr - 1] ^= (-1L);

			break;
		case OP_AND:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] &= stack[stack_ptr];
			}
			break;
		case OP_OR:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] |= stack[stack_ptr];
			}
			break;
		case OP_XOR:
			if (altera_check_stack(stack_ptr, 2, &status)) {
				--stack_ptr;
				stack[stack_ptr - 1] ^= stack[stack_ptr];
			}
			break;
		case OP_INV:
			if (!altera_check_stack(stack_ptr, 1, &status))
				break;
			stack[stack_ptr - 1] = stack[stack_ptr - 1] ? 0L : 1L;
			break;
		case OP_GT:
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			--stack_ptr;
			stack[stack_ptr - 1] =
				(stack[stack_ptr - 1] > stack[stack_ptr]) ?
									1L : 0L;

			break;
		case OP_LT:
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			--stack_ptr;
			stack[stack_ptr - 1] =
				(stack[stack_ptr - 1] < stack[stack_ptr]) ?
									1L : 0L;

			break;
		case OP_RET:
			if ((version > 0) && (stack_ptr == 0)) {
				/*
				 * We completed one of the main procedures
				 * of an ACTION.
				 * Find the next procedure
				 * to be executed and jump to it.
				 * If there are no more procedures, then EXIT.
				 */
				i = get_unaligned_be32(&p[proc_table +
						(13 * current_proc) + 4]);
				while ((i != 0) &&
					((proc_attributes[i] == 1) ||
					((proc_attributes[i] & 0xc0) == 0x40)))
					i = get_unaligned_be32(&p[proc_table +
								(13 * i) + 4]);

				if (i == 0) {
					/* no procedures to execute! */
					done = 1;
					*exit_code = 0;	/* success */
				} else {
					current_proc = i;
					pc = code_sect + get_unaligned_be32(
								&p[proc_table +
								(13 * i) + 9]);
					if ((pc < code_sect) ||
					    (pc >= debug_sect))
						status = -ERANGE;
				}

			} else
				if (altera_check_stack(stack_ptr, 1, &status)) {
					pc = stack[--stack_ptr] + code_sect;
					if ((pc <= code_sect) ||
					    (pc >= debug_sect))
						status = -ERANGE;

				}

			break;
		case OP_CMPS:
			/*
			 * Array short compare
			 * ...stack 0 is source 1 value
			 * ...stack 1 is source 2 value
			 * ...stack 2 is mask value
			 * ...stack 3 is count
			 */
			if (altera_check_stack(stack_ptr, 4, &status)) {
				s32 a = stack[--stack_ptr];
				s32 b = stack[--stack_ptr];
				long_tmp = stack[--stack_ptr];
				count = stack[stack_ptr - 1];

				if ((count < 1) || (count > 32))
					status = -ERANGE;
				else {
					long_tmp &= ((-1L) >> (32 - count));

					stack[stack_ptr - 1] =
					((a & long_tmp) == (b & long_tmp))
								? 1L : 0L;
				}
			}
			break;
		case OP_PINT:
			/*
			 * PRINT add integer
			 * ...stack 0 is integer value
			 */
			if (!altera_check_stack(stack_ptr, 1, &status))
				break;
			sprintf(&msg_buff[strlen(msg_buff)],
					"%ld", stack[--stack_ptr]);
			break;
		case OP_PRNT:
			/* PRINT finish */
			if (debug)
				printk(msg_buff, "\n");

			msg_buff[0] = '\0';
			break;
		case OP_DSS:
			/*
			 * DRSCAN short
			 * ...stack 0 is scan data
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			long_tmp = stack[--stack_ptr];
			count = stack[--stack_ptr];
			put_unaligned_le32(long_tmp, &charbuf[0]);
			status = altera_drscan(astate, count, charbuf, 0);
			break;
		case OP_DSSC:
			/*
			 * DRSCAN short with capture
			 * ...stack 0 is scan data
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			long_tmp = stack[--stack_ptr];
			count = stack[stack_ptr - 1];
			put_unaligned_le32(long_tmp, &charbuf[0]);
			status = altera_swap_dr(astate, count, charbuf,
							0, charbuf, 0);
			stack[stack_ptr - 1] = get_unaligned_le32(&charbuf[0]);
			break;
		case OP_ISS:
			/*
			 * IRSCAN short
			 * ...stack 0 is scan data
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			long_tmp = stack[--stack_ptr];
			count = stack[--stack_ptr];
			put_unaligned_le32(long_tmp, &charbuf[0]);
			status = altera_irscan(astate, count, charbuf, 0);
			break;
		case OP_ISSC:
			/*
			 * IRSCAN short with capture
			 * ...stack 0 is scan data
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			long_tmp = stack[--stack_ptr];
			count = stack[stack_ptr - 1];
			put_unaligned_le32(long_tmp, &charbuf[0]);
			status = altera_swap_ir(astate, count, charbuf,
							0, charbuf, 0);
			stack[stack_ptr - 1] = get_unaligned_le32(&charbuf[0]);
			break;
		case OP_DPR:
			if (!altera_check_stack(stack_ptr, 1, &status))
				break;
			count = stack[--stack_ptr];
			status = altera_set_dr_pre(&astate->js, count, 0, NULL);
			break;
		case OP_DPRL:
			/*
			 * DRPRE with literal data
			 * ...stack 0 is count
			 * ...stack 1 is literal data
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			count = stack[--stack_ptr];
			long_tmp = stack[--stack_ptr];
			put_unaligned_le32(long_tmp, &charbuf[0]);
			status = altera_set_dr_pre(&astate->js, count, 0,
						charbuf);
			break;
		case OP_DPO:
			/*
			 * DRPOST
			 * ...stack 0 is count
			 */
			if (altera_check_stack(stack_ptr, 1, &status)) {
				count = stack[--stack_ptr];
				status = altera_set_dr_post(&astate->js, count,
								0, NULL);
			}
			break;
		case OP_DPOL:
			/*
			 * DRPOST with literal data
			 * ...stack 0 is count
			 * ...stack 1 is literal data
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			count = stack[--stack_ptr];
			long_tmp = stack[--stack_ptr];
			put_unaligned_le32(long_tmp, &charbuf[0]);
			status = altera_set_dr_post(&astate->js, count, 0,
							charbuf);
			break;
		case OP_IPR:
			if (altera_check_stack(stack_ptr, 1, &status)) {
				count = stack[--stack_ptr];
				status = altera_set_ir_pre(&astate->js, count,
								0, NULL);
			}
			break;
		case OP_IPRL:
			/*
			 * IRPRE with literal data
			 * ...stack 0 is count
			 * ...stack 1 is literal data
			 */
			if (altera_check_stack(stack_ptr, 2, &status)) {
				count = stack[--stack_ptr];
				long_tmp = stack[--stack_ptr];
				put_unaligned_le32(long_tmp, &charbuf[0]);
				status = altera_set_ir_pre(&astate->js, count,
							0, charbuf);
			}
			break;
		case OP_IPO:
			/*
			 * IRPOST
			 * ...stack 0 is count
			 */
			if (altera_check_stack(stack_ptr, 1, &status)) {
				count = stack[--stack_ptr];
				status = altera_set_ir_post(&astate->js, count,
							0, NULL);
			}
			break;
		case OP_IPOL:
			/*
			 * IRPOST with literal data
			 * ...stack 0 is count
			 * ...stack 1 is literal data
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			count = stack[--stack_ptr];
			long_tmp = stack[--stack_ptr];
			put_unaligned_le32(long_tmp, &charbuf[0]);
			status = altera_set_ir_post(&astate->js, count, 0,
							charbuf);
			break;
		case OP_PCHR:
			if (altera_check_stack(stack_ptr, 1, &status)) {
				u8 ch;
				count = strlen(msg_buff);
				ch = (char) stack[--stack_ptr];
				if ((ch < 1) || (ch > 127)) {
					/*
					 * character code out of range
					 * instead of flagging an error,
					 * force the value to 127
					 */
					ch = 127;
				}
				msg_buff[count] = ch;
				msg_buff[count + 1] = '\0';
			}
			break;
		case OP_EXIT:
			if (altera_check_stack(stack_ptr, 1, &status))
				*exit_code = stack[--stack_ptr];

			done = 1;
			break;
		case OP_EQU:
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			--stack_ptr;
			stack[stack_ptr - 1] =
				(stack[stack_ptr - 1] == stack[stack_ptr]) ?
									1L : 0L;
			break;
		case OP_POPT:
			if (altera_check_stack(stack_ptr, 1, &status))
				--stack_ptr;

			break;
		case OP_ABS:
			if (!altera_check_stack(stack_ptr, 1, &status))
				break;
			if (stack[stack_ptr - 1] < 0)
				stack[stack_ptr - 1] = 0 - stack[stack_ptr - 1];

			break;
		case OP_BCH0:
			/*
			 * Batch operation 0
			 * SWP
			 * SWPN 7
			 * SWP
			 * SWPN 6
			 * DUPN 8
			 * SWPN 2
			 * SWP
			 * DUPN 6
			 * DUPN 6
			 */

			/* SWP  */
			if (altera_check_stack(stack_ptr, 2, &status)) {
				long_tmp = stack[stack_ptr - 2];
				stack[stack_ptr - 2] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}

			/* SWPN 7 */
			index = 7 + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				long_tmp = stack[stack_ptr - index];
				stack[stack_ptr - index] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}

			/* SWP  */
			if (altera_check_stack(stack_ptr, 2, &status)) {
				long_tmp = stack[stack_ptr - 2];
				stack[stack_ptr - 2] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}

			/* SWPN 6 */
			index = 6 + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				long_tmp = stack[stack_ptr - index];
				stack[stack_ptr - index] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}

			/* DUPN 8 */
			index = 8 + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				stack[stack_ptr] = stack[stack_ptr - index];
				++stack_ptr;
			}

			/* SWPN 2 */
			index = 2 + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				long_tmp = stack[stack_ptr - index];
				stack[stack_ptr - index] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}

			/* SWP  */
			if (altera_check_stack(stack_ptr, 2, &status)) {
				long_tmp = stack[stack_ptr - 2];
				stack[stack_ptr - 2] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}

			/* DUPN 6 */
			index = 6 + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				stack[stack_ptr] = stack[stack_ptr - index];
				++stack_ptr;
			}

			/* DUPN 6 */
			index = 6 + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				stack[stack_ptr] = stack[stack_ptr - index];
				++stack_ptr;
			}
			break;
		case OP_PSH0:
			stack[stack_ptr++] = 0;
			break;
		case OP_PSHL:
			stack[stack_ptr++] = (s32) args[0];
			break;
		case OP_PSHV:
			stack[stack_ptr++] = vars[args[0]];
			break;
		case OP_JMP:
			pc = args[0] + code_sect;
			if ((pc < code_sect) || (pc >= debug_sect))
				status = -ERANGE;
			break;
		case OP_CALL:
			stack[stack_ptr++] = pc;
			pc = args[0] + code_sect;
			if ((pc < code_sect) || (pc >= debug_sect))
				status = -ERANGE;
			break;
		case OP_NEXT:
			/*
			 * Process FOR / NEXT loop
			 * ...argument 0 is variable ID
			 * ...stack 0 is step value
			 * ...stack 1 is end value
			 * ...stack 2 is top address
			 */
			if (altera_check_stack(stack_ptr, 3, &status)) {
				s32 step = stack[stack_ptr - 1];
				s32 end = stack[stack_ptr - 2];
				s32 top = stack[stack_ptr - 3];
				s32 iterator = vars[args[0]];
				int break_out = 0;

				if (step < 0) {
					if (iterator <= end)
						break_out = 1;
				} else if (iterator >= end)
					break_out = 1;

				if (break_out) {
					stack_ptr -= 3;
				} else {
					vars[args[0]] = iterator + step;
					pc = top + code_sect;
					if ((pc < code_sect) ||
					    (pc >= debug_sect))
						status = -ERANGE;
				}
			}
			break;
		case OP_PSTR:
			/*
			 * PRINT add string
			 * ...argument 0 is string ID
			 */
			count = strlen(msg_buff);
			strlcpy(&msg_buff[count],
				&p[str_table + args[0]],
				ALTERA_MESSAGE_LENGTH - count);
			break;
		case OP_SINT:
			/*
			 * STATE intermediate state
			 * ...argument 0 is state code
			 */
			status = altera_goto_jstate(astate, args[0]);
			break;
		case OP_ST:
			/*
			 * STATE final state
			 * ...argument 0 is state code
			 */
			status = altera_goto_jstate(astate, args[0]);
			break;
		case OP_ISTP:
			/*
			 * IRSTOP state
			 * ...argument 0 is state code
			 */
			status = altera_set_irstop(&astate->js, args[0]);
			break;
		case OP_DSTP:
			/*
			 * DRSTOP state
			 * ...argument 0 is state code
			 */
			status = altera_set_drstop(&astate->js, args[0]);
			break;

		case OP_SWPN:
			/*
			 * Exchange top with Nth stack value
			 * ...argument 0 is 0-based stack entry
			 * to swap with top element
			 */
			index = (args[0]) + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				long_tmp = stack[stack_ptr - index];
				stack[stack_ptr - index] = stack[stack_ptr - 1];
				stack[stack_ptr - 1] = long_tmp;
			}
			break;
		case OP_DUPN:
			/*
			 * Duplicate Nth stack value
			 * ...argument 0 is 0-based stack entry to duplicate
			 */
			index = (args[0]) + 1;
			if (altera_check_stack(stack_ptr, index, &status)) {
				stack[stack_ptr] = stack[stack_ptr - index];
				++stack_ptr;
			}
			break;
		case OP_POPV:
			/*
			 * Pop stack into scalar variable
			 * ...argument 0 is variable ID
			 * ...stack 0 is value
			 */
			if (altera_check_stack(stack_ptr, 1, &status))
				vars[args[0]] = stack[--stack_ptr];

			break;
		case OP_POPE:
			/*
			 * Pop stack into integer array element
			 * ...argument 0 is variable ID
			 * ...stack 0 is array index
			 * ...stack 1 is value
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			variable_id = args[0];

			/*
			 * If variable is read-only,
			 * convert to writable array
			 */
			if ((version > 0) &&
				((attrs[variable_id] & 0x9c) == 0x1c)) {
				/* Allocate a writable buffer for this array */
				count = var_size[variable_id];
				long_tmp = vars[variable_id];
				longptr_tmp = kzalloc(count * sizeof(long),
								GFP_KERNEL);
				vars[variable_id] = (long)longptr_tmp;

				if (vars[variable_id] == 0) {
					status = -ENOMEM;
					break;
				}

				/* copy previous contents into buffer */
				for (i = 0; i < count; ++i) {
					longptr_tmp[i] =
						get_unaligned_be32(&p[long_tmp]);
					long_tmp += sizeof(long);
				}

				/*
				 * set bit 7 - buffer was
				 * dynamically allocated
				 */
				attrs[variable_id] |= 0x80;

				/* clear bit 2 - variable is writable */
				attrs[variable_id] &= ~0x04;
				attrs[variable_id] |= 0x01;

			}

			/* check that variable is a writable integer array */
			if ((attrs[variable_id] & 0x1c) != 0x18)
				status = -ERANGE;
			else {
				longptr_tmp = (long *)vars[variable_id];

				/* pop the array index */
				index = stack[--stack_ptr];

				/* pop the value and store it into the array */
				longptr_tmp[index] = stack[--stack_ptr];
			}

			break;
		case OP_POPA:
			/*
			 * Pop stack into Boolean array
			 * ...argument 0 is variable ID
			 * ...stack 0 is count
			 * ...stack 1 is array index
			 * ...stack 2 is value
			 */
			if (!altera_check_stack(stack_ptr, 3, &status))
				break;
			variable_id = args[0];

			/*
			 * If variable is read-only,
			 * convert to writable array
			 */
			if ((version > 0) &&
				((attrs[variable_id] & 0x9c) == 0x0c)) {
				/* Allocate a writable buffer for this array */
				long_tmp =
					(var_size[variable_id] + 7L) >> 3L;
				charptr_tmp2 = (u8 *)vars[variable_id];
				charptr_tmp =
					kzalloc(long_tmp, GFP_KERNEL);
				vars[variable_id] = (long)charptr_tmp;

				if (vars[variable_id] == 0) {
					status = -ENOMEM;
					break;
				}

				/* zero the buffer */
				for (long_idx = 0L;
					long_idx < long_tmp;
					++long_idx) {
					charptr_tmp[long_idx] = 0;
				}

				/* copy previous contents into buffer */
				for (long_idx = 0L;
					long_idx < var_size[variable_id];
					++long_idx) {
					long_idx2 = long_idx;

					if (charptr_tmp2[long_idx2 >> 3] &
						(1 << (long_idx2 & 7))) {
						charptr_tmp[long_idx >> 3] |=
							(1 << (long_idx & 7));
					}
				}

				/*
				 * set bit 7 - buffer was
				 * dynamically allocated
				 */
				attrs[variable_id] |= 0x80;

				/* clear bit 2 - variable is writable */
				attrs[variable_id] &= ~0x04;
				attrs[variable_id] |= 0x01;

			}

			/*
			 * check that variable is
			 * a writable Boolean array
			 */
			if ((attrs[variable_id] & 0x1c) != 0x08) {
				status = -ERANGE;
				break;
			}

			charptr_tmp = (u8 *)vars[variable_id];

			/* pop the count (number of bits to copy) */
			long_count = stack[--stack_ptr];

			/* pop the array index */
			long_idx = stack[--stack_ptr];

			reverse = 0;

			if (version > 0) {
				/*
				 * stack 0 = array right index
				 * stack 1 = array left index
				 */

				if (long_idx > long_count) {
					reverse = 1;
					long_tmp = long_count;
					long_count = 1 + long_idx -
								long_count;
					long_idx = long_tmp;

					/* reverse POPA is not supported */
					status = -ERANGE;
					break;
				} else
					long_count = 1 + long_count -
								long_idx;

			}

			/* pop the data */
			long_tmp = stack[--stack_ptr];

			if (long_count < 1) {
				status = -ERANGE;
				break;
			}

			for (i = 0; i < long_count; ++i) {
				if (long_tmp & (1L << (s32) i))
					charptr_tmp[long_idx >> 3L] |=
						(1L << (long_idx & 7L));
				else
					charptr_tmp[long_idx >> 3L] &=
						~(1L << (long_idx & 7L));

				++long_idx;
			}

			break;
		case OP_JMPZ:
			/*
			 * Pop stack and branch if zero
			 * ...argument 0 is address
			 * ...stack 0 is condition value
			 */
			if (altera_check_stack(stack_ptr, 1, &status)) {
				if (stack[--stack_ptr] == 0) {
					pc = args[0] + code_sect;
					if ((pc < code_sect) ||
					    (pc >= debug_sect))
						status = -ERANGE;
				}
			}
			break;
		case OP_DS:
		case OP_IS:
			/*
			 * DRSCAN
			 * IRSCAN
			 * ...argument 0 is scan data variable ID
			 * ...stack 0 is array index
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			long_idx = stack[--stack_ptr];
			long_count = stack[--stack_ptr];
			reverse = 0;
			if (version > 0) {
				/*
				 * stack 0 = array right index
				 * stack 1 = array left index
				 * stack 2 = count
				 */
				long_tmp = long_count;
				long_count = stack[--stack_ptr];

				if (long_idx > long_tmp) {
					reverse = 1;
					long_idx = long_tmp;
				}
			}

			charptr_tmp = (u8 *)vars[args[0]];

			if (reverse) {
				/*
				 * allocate a buffer
				 * and reverse the data order
				 */
				charptr_tmp2 = charptr_tmp;
				charptr_tmp = kzalloc((long_count >> 3) + 1,
								GFP_KERNEL);
				if (charptr_tmp == NULL) {
					status = -ENOMEM;
					break;
				}

				long_tmp = long_idx + long_count - 1;
				long_idx2 = 0;
				while (long_idx2 < long_count) {
					if (charptr_tmp2[long_tmp >> 3] &
							(1 << (long_tmp & 7)))
						charptr_tmp[long_idx2 >> 3] |=
							(1 << (long_idx2 & 7));
					else
						charptr_tmp[long_idx2 >> 3] &=
							~(1 << (long_idx2 & 7));

					--long_tmp;
					++long_idx2;
				}
			}

			if (opcode == 0x51) /* DS */
				status = altera_drscan(astate, long_count,
						charptr_tmp, long_idx);
			else /* IS */
				status = altera_irscan(astate, long_count,
						charptr_tmp, long_idx);

			if (reverse)
				kfree(charptr_tmp);

			break;
		case OP_DPRA:
			/*
			 * DRPRE with array data
			 * ...argument 0 is variable ID
			 * ...stack 0 is array index
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			index = stack[--stack_ptr];
			count = stack[--stack_ptr];

			if (version > 0)
				/*
				 * stack 0 = array right index
				 * stack 1 = array left index
				 */
				count = 1 + count - index;

			charptr_tmp = (u8 *)vars[args[0]];
			status = altera_set_dr_pre(&astate->js, count, index,
							charptr_tmp);
			break;
		case OP_DPOA:
			/*
			 * DRPOST with array data
			 * ...argument 0 is variable ID
			 * ...stack 0 is array index
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			index = stack[--stack_ptr];
			count = stack[--stack_ptr];

			if (version > 0)
				/*
				 * stack 0 = array right index
				 * stack 1 = array left index
				 */
				count = 1 + count - index;

			charptr_tmp = (u8 *)vars[args[0]];
			status = altera_set_dr_post(&astate->js, count, index,
							charptr_tmp);
			break;
		case OP_IPRA:
			/*
			 * IRPRE with array data
			 * ...argument 0 is variable ID
			 * ...stack 0 is array index
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			index = stack[--stack_ptr];
			count = stack[--stack_ptr];

			if (version > 0)
				/*
				 * stack 0 = array right index
				 * stack 1 = array left index
				 */
				count = 1 + count - index;

			charptr_tmp = (u8 *)vars[args[0]];
			status = altera_set_ir_pre(&astate->js, count, index,
							charptr_tmp);

			break;
		case OP_IPOA:
			/*
			 * IRPOST with array data
			 * ...argument 0 is variable ID
			 * ...stack 0 is array index
			 * ...stack 1 is count
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			index = stack[--stack_ptr];
			count = stack[--stack_ptr];

			if (version > 0)
				/*
				 * stack 0 = array right index
				 * stack 1 = array left index
				 */
				count = 1 + count - index;

			charptr_tmp = (u8 *)vars[args[0]];
			status = altera_set_ir_post(&astate->js, count, index,
							charptr_tmp);

			break;
		case OP_EXPT:
			/*
			 * EXPORT
			 * ...argument 0 is string ID
			 * ...stack 0 is integer expression
			 */
			if (altera_check_stack(stack_ptr, 1, &status)) {
				name = &p[str_table + args[0]];
				long_tmp = stack[--stack_ptr];
				altera_export_int(name, long_tmp);
			}
			break;
		case OP_PSHE:
			/*
			 * Push integer array element
			 * ...argument 0 is variable ID
			 * ...stack 0 is array index
			 */
			if (!altera_check_stack(stack_ptr, 1, &status))
				break;
			variable_id = args[0];
			index = stack[stack_ptr - 1];

			/* check variable type */
			if ((attrs[variable_id] & 0x1f) == 0x19) {
				/* writable integer array */
				longptr_tmp = (long *)vars[variable_id];
				stack[stack_ptr - 1] = longptr_tmp[index];
			} else if ((attrs[variable_id] & 0x1f) == 0x1c) {
				/* read-only integer array */
				long_tmp = vars[variable_id] +
						(index * sizeof(long));
				stack[stack_ptr - 1] =
					get_unaligned_be32(&p[long_tmp]);
			} else
				status = -ERANGE;

			break;
		case OP_PSHA:
			/*
			 * Push Boolean array
			 * ...argument 0 is variable ID
			 * ...stack 0 is count
			 * ...stack 1 is array index
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			variable_id = args[0];

			/* check that variable is a Boolean array */
			if ((attrs[variable_id] & 0x18) != 0x08) {
				status = -ERANGE;
				break;
			}

			charptr_tmp = (u8 *)vars[variable_id];

			/* pop the count (number of bits to copy) */
			count = stack[--stack_ptr];

			/* pop the array index */
			index = stack[stack_ptr - 1];

			if (version > 0)
				/*
				 * stack 0 = array right index
				 * stack 1 = array left index
				 */
				count = 1 + count - index;

			if ((count < 1) || (count > 32)) {
				status = -ERANGE;
				break;
			}

			long_tmp = 0L;

			for (i = 0; i < count; ++i)
				if (charptr_tmp[(i + index) >> 3] &
						(1 << ((i + index) & 7)))
					long_tmp |= (1L << i);

			stack[stack_ptr - 1] = long_tmp;

			break;
		case OP_DYNA:
			/*
			 * Dynamically change size of array
			 * ...argument 0 is variable ID
			 * ...stack 0 is new size
			 */
			if (!altera_check_stack(stack_ptr, 1, &status))
				break;
			variable_id = args[0];
			long_tmp = stack[--stack_ptr];

			if (long_tmp > var_size[variable_id]) {
				var_size[variable_id] = long_tmp;

				if (attrs[variable_id] & 0x10)
					/* allocate integer array */
					long_tmp *= sizeof(long);
				else
					/* allocate Boolean array */
					long_tmp = (long_tmp + 7) >> 3;

				/*
				 * If the buffer was previously allocated,
				 * free it
				 */
				if (attrs[variable_id] & 0x80) {
					kfree((void *)vars[variable_id]);
					vars[variable_id] = 0;
				}

				/*
				 * Allocate a new buffer
				 * of the requested size
				 */
				vars[variable_id] = (long)
					kzalloc(long_tmp, GFP_KERNEL);

				if (vars[variable_id] == 0) {
					status = -ENOMEM;
					break;
				}

				/*
				 * Set the attribute bit to indicate that
				 * this buffer was dynamically allocated and
				 * should be freed later
				 */
				attrs[variable_id] |= 0x80;

				/* zero out memory */
				count = ((var_size[variable_id] + 7L) /
									8L);
				charptr_tmp = (u8 *)(vars[variable_id]);
				for (index = 0; index < count; ++index)
					charptr_tmp[index] = 0;

			}

			break;
		case OP_EXPV:
			/*
			 * Export Boolean array
			 * ...argument 0 is string ID
			 * ...stack 0 is variable ID
			 * ...stack 1 is array right index
			 * ...stack 2 is array left index
			 */
			if (!altera_check_stack(stack_ptr, 3, &status))
				break;
			if (version == 0) {
				/* EXPV is not supported in JBC 1.0 */
				bad_opcode = 1;
				break;
			}
			name = &p[str_table + args[0]];
			variable_id = stack[--stack_ptr];
			long_idx = stack[--stack_ptr];/* right indx */
			long_idx2 = stack[--stack_ptr];/* left indx */

			if (long_idx > long_idx2) {
				/* reverse indices not supported */
				status = -ERANGE;
				break;
			}

			long_count = 1 + long_idx2 - long_idx;

			charptr_tmp = (u8 *)vars[variable_id];
			charptr_tmp2 = NULL;

			if ((long_idx & 7L) != 0) {
				s32 k = long_idx;
				charptr_tmp2 =
					kzalloc(((long_count + 7L) / 8L),
							GFP_KERNEL);
				if (charptr_tmp2 == NULL) {
					status = -ENOMEM;
					break;
				}

				for (i = 0; i < long_count; ++i) {
					if (charptr_tmp[k >> 3] &
							(1 << (k & 7)))
						charptr_tmp2[i >> 3] |=
								(1 << (i & 7));
					else
						charptr_tmp2[i >> 3] &=
								~(1 << (i & 7));

					++k;
				}
				charptr_tmp = charptr_tmp2;

			} else if (long_idx != 0)
				charptr_tmp = &charptr_tmp[long_idx >> 3];

			altera_export_bool_array(name, charptr_tmp,
							long_count);

			/* free allocated buffer */
			if ((long_idx & 7L) != 0)
				kfree(charptr_tmp2);

			break;
		case OP_COPY: {
			/*
			 * Array copy
			 * ...argument 0 is dest ID
			 * ...argument 1 is source ID
			 * ...stack 0 is count
			 * ...stack 1 is dest index
			 * ...stack 2 is source index
			 */
			s32 copy_count;
			s32 copy_index;
			s32 copy_index2;
			s32 destleft;
			s32 src_count;
			s32 dest_count;
			int src_reverse = 0;
			int dest_reverse = 0;

			if (!altera_check_stack(stack_ptr, 3, &status))
				break;

			copy_count = stack[--stack_ptr];
			copy_index = stack[--stack_ptr];
			copy_index2 = stack[--stack_ptr];
			reverse = 0;

			if (version > 0) {
				/*
				 * stack 0 = source right index
				 * stack 1 = source left index
				 * stack 2 = destination right index
				 * stack 3 = destination left index
				 */
				destleft = stack[--stack_ptr];

				if (copy_count > copy_index) {
					src_reverse = 1;
					reverse = 1;
					src_count = 1 + copy_count - copy_index;
					/* copy_index = source start index */
				} else {
					src_count = 1 + copy_index - copy_count;
					/* source start index */
					copy_index = copy_count;
				}

				if (copy_index2 > destleft) {
					dest_reverse = 1;
					reverse = !reverse;
					dest_count = 1 + copy_index2 - destleft;
					/* destination start index */
					copy_index2 = destleft;
				} else
					dest_count = 1 + destleft - copy_index2;

				copy_count = (src_count < dest_count) ?
							src_count : dest_count;

				if ((src_reverse || dest_reverse) &&
					(src_count != dest_count))
					/*
					 * If either the source or destination
					 * is reversed, we can't tolerate
					 * a length mismatch, because we
					 * "left justify" arrays when copying.
					 * This won't work correctly
					 * with reversed arrays.
					 */
					status = -ERANGE;

			}

			count = copy_count;
			index = copy_index;
			index2 = copy_index2;

			/*
			 * If destination is a read-only array,
			 * allocate a buffer and convert it to a writable array
			 */
			variable_id = args[1];
			if ((version > 0) &&
				((attrs[variable_id] & 0x9c) == 0x0c)) {
				/* Allocate a writable buffer for this array */
				long_tmp =
					(var_size[variable_id] + 7L) >> 3L;
				charptr_tmp2 = (u8 *)vars[variable_id];
				charptr_tmp =
					kzalloc(long_tmp, GFP_KERNEL);
				vars[variable_id] = (long)charptr_tmp;

				if (vars[variable_id] == 0) {
					status = -ENOMEM;
					break;
				}

				/* zero the buffer */
				for (long_idx = 0L; long_idx < long_tmp;
								++long_idx)
					charptr_tmp[long_idx] = 0;

				/* copy previous contents into buffer */
				for (long_idx = 0L;
					long_idx < var_size[variable_id];
								++long_idx) {
					long_idx2 = long_idx;

					if (charptr_tmp2[long_idx2 >> 3] &
						(1 << (long_idx2 & 7)))
						charptr_tmp[long_idx >> 3] |=
							(1 << (long_idx & 7));

				}

				/*
				set bit 7 - buffer was dynamically allocated */
				attrs[variable_id] |= 0x80;

				/* clear bit 2 - variable is writable */
				attrs[variable_id] &= ~0x04;
				attrs[variable_id] |= 0x01;
			}

			charptr_tmp = (u8 *)vars[args[1]];
			charptr_tmp2 = (u8 *)vars[args[0]];

			/* check if destination is a writable Boolean array */
			if ((attrs[args[1]] & 0x1c) != 0x08) {
				status = -ERANGE;
				break;
			}

			if (count < 1) {
				status = -ERANGE;
				break;
			}

			if (reverse)
				index2 += (count - 1);

			for (i = 0; i < count; ++i) {
				if (charptr_tmp2[index >> 3] &
							(1 << (index & 7)))
					charptr_tmp[index2 >> 3] |=
							(1 << (index2 & 7));
				else
					charptr_tmp[index2 >> 3] &=
						~(1 << (index2 & 7));

				++index;
				if (reverse)
					--index2;
				else
					++index2;
			}

			break;
		}
		case OP_DSC:
		case OP_ISC: {
			/*
			 * DRSCAN with capture
			 * IRSCAN with capture
			 * ...argument 0 is scan data variable ID
			 * ...argument 1 is capture variable ID
			 * ...stack 0 is capture index
			 * ...stack 1 is scan data index
			 * ...stack 2 is count
			 */
			s32 scan_right, scan_left;
			s32 capture_count = 0;
			s32 scan_count = 0;
			s32 capture_index;
			s32 scan_index;

			if (!altera_check_stack(stack_ptr, 3, &status))
				break;

			capture_index = stack[--stack_ptr];
			scan_index = stack[--stack_ptr];

			if (version > 0) {
				/*
				 * stack 0 = capture right index
				 * stack 1 = capture left index
				 * stack 2 = scan right index
				 * stack 3 = scan left index
				 * stack 4 = count
				 */
				scan_right = stack[--stack_ptr];
				scan_left = stack[--stack_ptr];
				capture_count = 1 + scan_index - capture_index;
				scan_count = 1 + scan_left - scan_right;
				scan_index = scan_right;
			}

			long_count = stack[--stack_ptr];
			/*
			 * If capture array is read-only, allocate a buffer
			 * and convert it to a writable array
			 */
			variable_id = args[1];
			if ((version > 0) &&
				((attrs[variable_id] & 0x9c) == 0x0c)) {
				/* Allocate a writable buffer for this array */
				long_tmp =
					(var_size[variable_id] + 7L) >> 3L;
				charptr_tmp2 = (u8 *)vars[variable_id];
				charptr_tmp =
					kzalloc(long_tmp, GFP_KERNEL);
				vars[variable_id] = (long)charptr_tmp;

				if (vars[variable_id] == 0) {
					status = -ENOMEM;
					break;
				}

				/* zero the buffer */
				for (long_idx = 0L; long_idx < long_tmp;
								++long_idx)
					charptr_tmp[long_idx] = 0;

				/* copy previous contents into buffer */
				for (long_idx = 0L;
					long_idx < var_size[variable_id];
								++long_idx) {
					long_idx2 = long_idx;

					if (charptr_tmp2[long_idx2 >> 3] &
						(1 << (long_idx2 & 7)))
						charptr_tmp[long_idx >> 3] |=
							(1 << (long_idx & 7));

				}

				/*
				 * set bit 7 - buffer was
				 * dynamically allocated
				 */
				attrs[variable_id] |= 0x80;

				/* clear bit 2 - variable is writable */
				attrs[variable_id] &= ~0x04;
				attrs[variable_id] |= 0x01;

			}

			charptr_tmp = (u8 *)vars[args[0]];
			charptr_tmp2 = (u8 *)vars[args[1]];

			if ((version > 0) &&
					((long_count > capture_count) ||
					(long_count > scan_count))) {
				status = -ERANGE;
				break;
			}

			/*
			 * check that capture array
			 * is a writable Boolean array
			 */
			if ((attrs[args[1]] & 0x1c) != 0x08) {
				status = -ERANGE;
				break;
			}

			if (status == 0) {
				if (opcode == 0x82) /* DSC */
					status = altera_swap_dr(astate,
							long_count,
							charptr_tmp,
							scan_index,
							charptr_tmp2,
							capture_index);
				else /* ISC */
					status = altera_swap_ir(astate,
							long_count,
							charptr_tmp,
							scan_index,
							charptr_tmp2,
							capture_index);

			}

			break;
		}
		case OP_WAIT:
			/*
			 * WAIT
			 * ...argument 0 is wait state
			 * ...argument 1 is end state
			 * ...stack 0 is cycles
			 * ...stack 1 is microseconds
			 */
			if (!altera_check_stack(stack_ptr, 2, &status))
				break;
			long_tmp = stack[--stack_ptr];

			if (long_tmp != 0L)
				status = altera_wait_cycles(astate, long_tmp,
								args[0]);

			long_tmp = stack[--stack_ptr];

			if ((status == 0) && (long_tmp != 0L))
				status = altera_wait_msecs(astate,
								long_tmp,
								args[0]);

			if ((status == 0) && (args[1] != args[0]))
				status = altera_goto_jstate(astate,
								args[1]);

			if (version > 0) {
				--stack_ptr; /* throw away MAX cycles */
				--stack_ptr; /* throw away MAX microseconds */
			}
			break;
		case OP_CMPA: {
			/*
			 * Array compare
			 * ...argument 0 is source 1 ID
			 * ...argument 1 is source 2 ID
			 * ...argument 2 is mask ID
			 * ...stack 0 is source 1 index
			 * ...stack 1 is source 2 index
			 * ...stack 2 is mask index
			 * ...stack 3 is count
			 */
			s32 a, b;
			u8 *source1 = (u8 *)vars[args[0]];
			u8 *source2 = (u8 *)vars[args[1]];
			u8 *mask = (u8 *)vars[args[2]];
			u32 index1;
			u32 index2;
			u32 mask_index;

			if (!altera_check_stack(stack_ptr, 4, &status))
				break;

			index1 = stack[--stack_ptr];
			index2 = stack[--stack_ptr];
			mask_index = stack[--stack_ptr];
			long_count = stack[--stack_ptr];

			if (version > 0) {
				/*
				 * stack 0 = source 1 right index
				 * stack 1 = source 1 left index
				 * stack 2 = source 2 right index
				 * stack 3 = source 2 left index
				 * stack 4 = mask right index
				 * stack 5 = mask left index
				 */
				s32 mask_right = stack[--stack_ptr];
				s32 mask_left = stack[--stack_ptr];
				/* source 1 count */
				a = 1 + index2 - index1;
				/* source 2 count */
				b = 1 + long_count - mask_index;
				a = (a < b) ? a : b;
				/* mask count */
				b = 1 + mask_left - mask_right;
				a = (a < b) ? a : b;
				/* source 2 start index */
				index2 = mask_index;
				/* mask start index */
				mask_index = mask_right;
				long_count = a;
			}

			long_tmp = 1L;

			if (long_count < 1)
				status = -ERANGE;
			else {
				count = long_count;

				for (i = 0; i < count; ++i) {
					if (mask[mask_index >> 3] &
						(1 << (mask_index & 7))) {
						a = source1[index1 >> 3] &
							(1 << (index1 & 7))
								? 1 : 0;
						b = source2[index2 >> 3] &
							(1 << (index2 & 7))
								? 1 : 0;

						if (a != b) /* failure */
							long_tmp = 0L;
					}
					++index1;
					++index2;
					++mask_index;
				}
			}

			stack[stack_ptr++] = long_tmp;

			break;
		}
		default:
			/* Unrecognized opcode -- ERROR! */
			bad_opcode = 1;
			break;
		}

		if (bad_opcode)
			status = -ENOSYS;

		if ((stack_ptr < 0) || (stack_ptr >= ALTERA_STACK_SIZE))
			status = -EOVERFLOW;

		if (status != 0) {
			done = 1;
			*error_address = (s32)(opcode_address - code_sect);
		}
	}

	altera_free_buffers(astate);

	/* Free all dynamically allocated arrays */
	if ((attrs != NULL) && (vars != NULL))
		for (i = 0; i < sym_count; ++i)
			if (attrs[i] & 0x80)
				kfree((void *)vars[i]);

	kfree(vars);
	kfree(var_size);
	kfree(attrs);
	kfree(proc_attributes);

	return status;
}

static int altera_get_note(u8 *p, s32 program_size,
			s32 *offset, char *key, char *value, int length)
/*
 * Gets key and value of NOTE fields in the JBC file.
 * Can be called in two modes:  if offset pointer is NULL,
 * then the function searches for note fields which match
 * the key string provided.  If offset is not NULL, then
 * the function finds the next note field of any key,
 * starting at the offset specified by the offset pointer.
 * Returns 0 for success, else appropriate error code
 */
{
	int status = -ENODATA;
	u32 note_strings = 0L;
	u32 note_table = 0L;
	u32 note_count = 0L;
	u32 first_word = 0L;
	int version = 0;
	int delta = 0;
	char *key_ptr;
	char *value_ptr;
	int i;

	/* Read header information */
	if (program_size > 52L) {
		first_word    = get_unaligned_be32(&p[0]);
		version = (first_word & 1L);
		delta = version * 8;

		note_strings  = get_unaligned_be32(&p[8 + delta]);
		note_table    = get_unaligned_be32(&p[12 + delta]);
		note_count    = get_unaligned_be32(&p[44 + (2 * delta)]);
	}

	if ((first_word != 0x4A414D00L) && (first_word != 0x4A414D01L))
		return -EIO;

	if (note_count <= 0L)
		return status;

	if (offset == NULL) {
		/*
		 * We will search for the first note with a specific key,
		 * and return only the value
		 */
		for (i = 0; (i < note_count) &&
						(status != 0); ++i) {
			key_ptr = &p[note_strings +
					get_unaligned_be32(
					&p[note_table + (8 * i)])];
			if ((strnicmp(key, key_ptr, strlen(key_ptr)) == 0) &&
						(key != NULL)) {
				status = 0;

				value_ptr = &p[note_strings +
						get_unaligned_be32(
						&p[note_table + (8 * i) + 4])];

				if (value != NULL)
					strlcpy(value, value_ptr, length);

			}
		}
	} else {
		/*
		 * We will search for the next note, regardless of the key,
		 * and return both the value and the key
		 */

		i = *offset;

		if ((i >= 0) && (i < note_count)) {
			status = 0;

			if (key != NULL)
				strlcpy(key, &p[note_strings +
						get_unaligned_be32(
						&p[note_table + (8 * i)])],
					length);

			if (value != NULL)
				strlcpy(value, &p[note_strings +
						get_unaligned_be32(
						&p[note_table + (8 * i) + 4])],
					length);

			*offset = i + 1;
		}
	}

	return status;
}

static int altera_check_crc(u8 *p, s32 program_size)
{
	int status = 0;
	u16 local_expected = 0,
	    local_actual = 0,
	    shift_reg = 0xffff;
	int bit, feedback;
	u8 databyte;
	u32 i;
	u32 crc_section = 0L;
	u32 first_word = 0L;
	int version = 0;
	int delta = 0;

	if (program_size > 52L) {
		first_word  = get_unaligned_be32(&p[0]);
		version = (first_word & 1L);
		delta = version * 8;

		crc_section = get_unaligned_be32(&p[32 + delta]);
	}

	if ((first_word != 0x4A414D00L) && (first_word != 0x4A414D01L))
		status = -EIO;

	if (crc_section >= program_size)
		status = -EIO;

	if (status == 0) {
		local_expected = (u16)get_unaligned_be16(&p[crc_section]);

		for (i = 0; i < crc_section; ++i) {
			databyte = p[i];
			for (bit = 0; bit < 8; bit++) {
				feedback = (databyte ^ shift_reg) & 0x01;
				shift_reg >>= 1;
				if (feedback)
					shift_reg ^= 0x8408;

				databyte >>= 1;
			}
		}

		local_actual = (u16)~shift_reg;

		if (local_expected != local_actual)
			status = -EILSEQ;

	}

	if (debug || status) {
		switch (status) {
		case 0:
			printk(KERN_INFO "%s: CRC matched: %04x\n", __func__,
				local_actual);
			break;
		case -EILSEQ:
			printk(KERN_ERR "%s: CRC mismatch: expected %04x, "
				"actual %04x\n", __func__, local_expected,
				local_actual);
			break;
		case -ENODATA:
			printk(KERN_ERR "%s: expected CRC not found, "
				"actual CRC = %04x\n", __func__,
				local_actual);
			break;
		case -EIO:
			printk(KERN_ERR "%s: error: format isn't "
				"recognized.\n", __func__);
			break;
		default:
			printk(KERN_ERR "%s: CRC function returned error "
				"code %d\n", __func__, status);
			break;
		}
	}

	return status;
}

static int altera_get_file_info(u8 *p,
					s32 program_size,
					int *format_version,
					int *action_count,
					int *procedure_count)
{
	int status = -EIO;
	u32 first_word = 0;
	int version = 0;

	if (program_size <= 52L)
		return status;

	first_word = get_unaligned_be32(&p[0]);

	if ((first_word == 0x4A414D00L) || (first_word == 0x4A414D01L)) {
		status = 0;

		version = (first_word & 1L);
		*format_version = version + 1;

		if (version > 0) {
			*action_count = get_unaligned_be32(&p[48]);
			*procedure_count = get_unaligned_be32(&p[52]);
		}
	}

	return status;
}

static int altera_get_act_info(u8 *p,
					s32 program_size,
					int index,
					char **name,
					char **description,
					struct altera_procinfo **proc_list)
{
	int status = -EIO;
	struct altera_procinfo *procptr = NULL;
	struct altera_procinfo *tmpptr = NULL;
	u32 first_word = 0L;
	u32 action_table = 0L;
	u32 proc_table = 0L;
	u32 str_table = 0L;
	u32 note_strings = 0L;
	u32 action_count = 0L;
	u32 proc_count = 0L;
	u32 act_name_id = 0L;
	u32 act_desc_id = 0L;
	u32 act_proc_id = 0L;
	u32 act_proc_name = 0L;
	u8 act_proc_attribute = 0;

	if (program_size <= 52L)
		return status;
	/* Read header information */
	first_word = get_unaligned_be32(&p[0]);

	if (first_word != 0x4A414D01L)
		return status;

	action_table = get_unaligned_be32(&p[4]);
	proc_table   = get_unaligned_be32(&p[8]);
	str_table = get_unaligned_be32(&p[12]);
	note_strings = get_unaligned_be32(&p[16]);
	action_count = get_unaligned_be32(&p[48]);
	proc_count   = get_unaligned_be32(&p[52]);

	if (index >= action_count)
		return status;

	act_name_id = get_unaligned_be32(&p[action_table + (12 * index)]);
	act_desc_id = get_unaligned_be32(&p[action_table + (12 * index) + 4]);
	act_proc_id = get_unaligned_be32(&p[action_table + (12 * index) + 8]);

	*name = &p[str_table + act_name_id];

	if (act_desc_id < (note_strings - str_table))
		*description = &p[str_table + act_desc_id];

	do {
		act_proc_name = get_unaligned_be32(
					&p[proc_table + (13 * act_proc_id)]);
		act_proc_attribute =
			(p[proc_table + (13 * act_proc_id) + 8] & 0x03);

		procptr = (struct altera_procinfo *)
				kzalloc(sizeof(struct altera_procinfo),
								GFP_KERNEL);

		if (procptr == NULL)
			status = -ENOMEM;
		else {
			procptr->name = &p[str_table + act_proc_name];
			procptr->attrs = act_proc_attribute;
			procptr->next = NULL;

			/* add record to end of linked list */
			if (*proc_list == NULL)
				*proc_list = procptr;
			else {
				tmpptr = *proc_list;
				while (tmpptr->next != NULL)
					tmpptr = tmpptr->next;
				tmpptr->next = procptr;
			}
		}

		act_proc_id = get_unaligned_be32(
				&p[proc_table + (13 * act_proc_id) + 4]);
	} while ((act_proc_id != 0) && (act_proc_id < proc_count));

	return status;
}

int altera_init(struct altera_config *config, const struct firmware *fw)
{
	struct altera_state *astate = NULL;
	struct altera_procinfo *proc_list = NULL;
	struct altera_procinfo *procptr = NULL;
	char *key = NULL;
	char *value = NULL;
	char *action_name = NULL;
	char *description = NULL;
	int exec_result = 0;
	int exit_code = 0;
	int format_version = 0;
	int action_count = 0;
	int procedure_count = 0;
	int index = 0;
	s32 offset = 0L;
	s32 error_address = 0L;

	key = kzalloc(33 * sizeof(char), GFP_KERNEL);
	if (!key)
		return -ENOMEM;
	value = kzalloc(257 * sizeof(char), GFP_KERNEL);
	if (!value)
		return -ENOMEM;
	astate = kzalloc(sizeof(struct altera_state), GFP_KERNEL);
	if (!astate)
		return -ENOMEM;

	astate->config = config;
	if (!astate->config->jtag_io) {
		dprintk(KERN_INFO "%s: using byteblaster!\n", __func__);
		astate->config->jtag_io = netup_jtag_io_lpt;
	}

	altera_check_crc((u8 *)fw->data, fw->size);

	if (debug) {
		altera_get_file_info((u8 *)fw->data, fw->size, &format_version,
					&action_count, &procedure_count);
		printk(KERN_INFO "%s: File format is %s ByteCode format\n",
			__func__, (format_version == 2) ? "Jam STAPL" :
						"pre-standardized Jam 1.1");
		while (altera_get_note((u8 *)fw->data, fw->size,
					&offset, key, value, 256) == 0)
			printk(KERN_INFO "%s: NOTE \"%s\" = \"%s\"\n",
					__func__, key, value);
	}

	if (debug && (format_version == 2) && (action_count > 0)) {
		printk(KERN_INFO "%s: Actions available:\n", __func__);
		for (index = 0; index < action_count; ++index) {
			altera_get_act_info((u8 *)fw->data, fw->size,
						index, &action_name,
						&description,
						&proc_list);

			if (description == NULL)
				printk(KERN_INFO "%s: %s\n",
						__func__,
						action_name);
			else
				printk(KERN_INFO "%s: %s \"%s\"\n",
						__func__,
						action_name,
						description);

			procptr = proc_list;
			while (procptr != NULL) {
				if (procptr->attrs != 0)
					printk(KERN_INFO "%s:    %s (%s)\n",
						__func__,
						procptr->name,
						(procptr->attrs == 1) ?
						"optional" : "recommended");

				proc_list = procptr->next;
				kfree(procptr);
				procptr = proc_list;
			}
		}

		printk(KERN_INFO "\n");
	}

	exec_result = altera_execute(astate, (u8 *)fw->data, fw->size,
				&error_address, &exit_code, &format_version);

	if (exit_code)
		exec_result = -EREMOTEIO;

	if ((format_version == 2) && (exec_result == -EINVAL)) {
		if (astate->config->action == NULL)
			printk(KERN_ERR "%s: error: no action specified for "
				"Jam STAPL file.\nprogram terminated.\n",
				__func__);
		else
			printk(KERN_ERR "%s: error: action \"%s\""
				" is not supported "
				"for this Jam STAPL file.\n"
				"Program terminated.\n", __func__,
				astate->config->action);

	} else if (exec_result)
		printk(KERN_ERR "%s: error %d\n", __func__, exec_result);

	kfree(key);
	kfree(value);
	kfree(astate);

	return 0;
}
EXPORT_SYMBOL(altera_init);
