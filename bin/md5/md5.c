/*	$OpenBSD: md5.c,v 1.99 2025/07/17 05:30:41 tb Exp $	*/

/*
 * Copyright (c) 2001,2003,2005-2007,2010,2013,2014
 *	Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <md5.h>
#include <rmd160.h>
#include <sha1.h>
#include <sha2.h>
#include <crc.h>

#define STYLE_MD5	0
#define STYLE_CKSUM	1
#define STYLE_TERSE	2

#define MAX_DIGEST_LEN	128

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

union ANY_CTX {
#if !defined(SHA2_ONLY)
	CKSUM_CTX cksum;
	MD5_CTX md5;
	RMD160_CTX rmd160;
	SHA1_CTX sha1;
#endif /* !defined(SHA2_ONLY) */
	SHA2_CTX sha2;
};

#if !defined(SHA2_ONLY)
static void
cksum_init(union ANY_CTX *ctx)
{
	CKSUM_Init(&ctx->cksum);
}

static void
cksum_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	CKSUM_Update(&ctx->cksum, data, len);
}

static void
cksum_final(unsigned char *digest, union ANY_CTX *ctx)
{
	CKSUM_Final(&ctx->cksum);
}

static char *
cksum_end(union ANY_CTX *ctx, char *digest)
{
	return CKSUM_End(&ctx->cksum, digest);
}

static void
md5_init(union ANY_CTX *ctx)
{
	MD5Init(&ctx->md5);
}

static void
md5_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	MD5Update(&ctx->md5, data, len);
}

static void
md5_final(unsigned char *digest, union ANY_CTX *ctx)
{
	MD5Final(digest, &ctx->md5);
}

static char *
md5_end(union ANY_CTX *ctx, char *digest)
{
	return MD5End(&ctx->md5, digest);
}

static void
rmd160_init(union ANY_CTX *ctx)
{
	RMD160Init(&ctx->rmd160);
}

static void
rmd160_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	RMD160Update(&ctx->rmd160, data, len);
}

static void
rmd160_final(unsigned char *digest, union ANY_CTX *ctx)
{
	RMD160Final(digest, &ctx->rmd160);
}

static char *
rmd160_end(union ANY_CTX *ctx, char *digest)
{
	return RMD160End(&ctx->rmd160, digest);
}

static void
sha1_init(union ANY_CTX *ctx)
{
	SHA1Init(&ctx->sha1);
}

static void
sha1_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA1Update(&ctx->sha1, data, len);
}

static void
sha1_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA1Final(digest, &ctx->sha1);
}

static char *
sha1_end(union ANY_CTX *ctx, char *digest)
{
	return SHA1End(&ctx->sha1, digest);
}

static void
sha224_init(union ANY_CTX *ctx)
{
	SHA224Init(&ctx->sha2);
}

static void
sha224_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA224Update(&ctx->sha2, data, len);
}

static void
sha224_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA224Final(digest, &ctx->sha2);
}

static char *
sha224_end(union ANY_CTX *ctx, char *digest)
{
	return SHA224End(&ctx->sha2, digest);
}
#endif /* !defined(SHA2_ONLY) */

static void
sha256_init(union ANY_CTX *ctx)
{
	SHA256Init(&ctx->sha2);
}

static void
sha256_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA256Update(&ctx->sha2, data, len);
}

static void
sha256_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA256Final(digest, &ctx->sha2);
}

static char *
sha256_end(union ANY_CTX *ctx, char *digest)
{
	return SHA256End(&ctx->sha2, digest);
}

#if !defined(SHA2_ONLY)
static void
sha384_init(union ANY_CTX *ctx)
{
	SHA384Init(&ctx->sha2);
}

static void
sha384_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA384Update(&ctx->sha2, data, len);
}

static void
sha384_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA384Final(digest, &ctx->sha2);
}

static char *
sha384_end(union ANY_CTX *ctx, char *digest)
{
	return SHA384End(&ctx->sha2, digest);
}

static void
sha512_256_init(union ANY_CTX *ctx)
{
	SHA512_256Init(&ctx->sha2);
}

static void
sha512_256_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA512_256Update(&ctx->sha2, data, len);
}

static void
sha512_256_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA512_256Final(digest, &ctx->sha2);
}

static char *
sha512_256_end(union ANY_CTX *ctx, char *digest)
{
	return SHA512_256End(&ctx->sha2, digest);
}
#endif /* !defined(SHA2_ONLY) */

static void
sha512_init(union ANY_CTX *ctx)
{
	SHA512Init(&ctx->sha2);
}

static void
sha512_update(union ANY_CTX *ctx, const unsigned char *data, size_t len)
{
	SHA512Update(&ctx->sha2, data, len);
}

static void
sha512_final(unsigned char *digest, union ANY_CTX *ctx)
{
	SHA512Final(digest, &ctx->sha2);
}

static char *
sha512_end(union ANY_CTX *ctx, char *digest)
{
	return SHA512End(&ctx->sha2, digest);
}

struct hash_function {
	const char *name;
	size_t digestlen;
	int style;
	int base64;
	void *ctx;	/* XXX - only used by digest_file() */
	void (*init)(union ANY_CTX *);
	void (*update)(union ANY_CTX *, const unsigned char *, size_t);
	void (*final)(unsigned char *, union ANY_CTX *);
	char * (*end)(union ANY_CTX *, char *);
	TAILQ_ENTRY(hash_function) tailq;
} functions[] = {
#if !defined(SHA2_ONLY)
	{
		"CKSUM",
		CKSUM_DIGEST_LENGTH,
		STYLE_CKSUM,
		-1,
		NULL,
		cksum_init,
		cksum_update,
		cksum_final,
		cksum_end,
	},
	{
		"MD5",
		MD5_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		md5_init,
		md5_update,
		md5_final,
		md5_end,
	},
	{
		"RMD160",
		RMD160_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		rmd160_init,
		rmd160_update,
		rmd160_final,
		rmd160_end,
	},
	{
		"SHA1",
		SHA1_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		sha1_init,
		sha1_update,
		sha1_final,
		sha1_end,
	},
	{
		"SHA224",
		SHA224_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		sha224_init,
		sha224_update,
		sha224_final,
		sha224_end,
	},
#endif /* !defined(SHA2_ONLY) */
	{
		"SHA256",
		SHA256_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		sha256_init,
		sha256_update,
		sha256_final,
		sha256_end,
	},
#if !defined(SHA2_ONLY)
	{
		"SHA384",
		SHA384_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		sha384_init,
		sha384_update,
		sha384_final,
		sha384_end,
	},
	{
		"SHA512/256",
		SHA512_256_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		sha512_256_init,
		sha512_256_update,
		sha512_256_final,
		sha512_256_end,
	},
#endif /* !defined(SHA2_ONLY) */
	{
		"SHA512",
		SHA512_DIGEST_LENGTH,
		STYLE_MD5,
		0,
		NULL,
		sha512_init,
		sha512_update,
		sha512_final,
		sha512_end,
	},
	{
		NULL,
	}
};

TAILQ_HEAD(hash_list, hash_function);

void digest_end(const struct hash_function *, void *, char *, size_t, int);
int  digest_file(const char *, struct hash_list *, int);
void digest_print(const struct hash_function *, const char *, const char *);
#if !defined(SHA2_ONLY)
int  digest_filelist(const char *, struct hash_function *, int, char **);
void digest_printstr(const struct hash_function *, const char *, const char *);
void digest_string(char *, struct hash_list *);
void digest_test(struct hash_list *);
void digest_time(struct hash_list *, int);
#endif /* !defined(SHA2_ONLY) */
void hash_insert(struct hash_list *, struct hash_function *, int);
void usage(void) __attribute__((__noreturn__));

extern char *__progname;
int qflag = 0;
FILE *ofile = NULL;

int
main(int argc, char **argv)
{
	struct hash_function *hf, *hftmp;
	struct hash_list hl;
	size_t len;
	char *cp, *input_string, *selective_checklist;
	const char *optstr;
	int fl, error, base64;
	int bflag, cflag, pflag, rflag, tflag, xflag;

	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	TAILQ_INIT(&hl);
	input_string = NULL;
	selective_checklist = NULL;
	error = bflag = cflag = pflag = qflag = rflag = tflag = xflag = 0;

#if !defined(SHA2_ONLY)
	if (strcmp(__progname, "cksum") == 0)
		optstr = "a:bC:ch:pqrs:tx";
	else
#endif /* !defined(SHA2_ONLY) */
		optstr = "bC:ch:pqrs:tx";

	/* Check for -b option early since it changes behavior. */
	while ((fl = getopt(argc, argv, optstr)) != -1) {
		switch (fl) {
		case 'b':
			bflag = 1;
			break;
		case '?':
			usage();
		}
	}
	optind = 1;
	optreset = 1;
	while ((fl = getopt(argc, argv, optstr)) != -1) {
		switch (fl) {
		case 'a':
			while ((cp = strsep(&optarg, " \t,")) != NULL) {
				if (*cp == '\0')
					continue;
				base64 = -1;
				for (hf = functions; hf->name != NULL; hf++) {
					len = strlen(hf->name);
					if (strncasecmp(cp, hf->name, len) != 0)
						continue;
					if (cp[len] == '\0') {
						if (hf->base64 != -1)
							base64 = bflag;
						break;	/* exact match */
					}
					if (cp[len + 1] == '\0' &&
					    (cp[len] == 'b' || cp[len] == 'x')) {
						base64 =
						    cp[len] == 'b' ?  1 : 0;
						break;	/* match w/ suffix */
					}
				}
				if (hf->name == NULL) {
					warnx("unknown algorithm \"%s\"", cp);
					usage();
				}
				if (hf->base64 == -1 && base64 != -1) {
					warnx("%s doesn't support %s",
					    hf->name,
					    base64 ? "base64" : "hex");
					usage();
				}
				/* Check for dupes. */
				TAILQ_FOREACH(hftmp, &hl, tailq) {
					if (hftmp->base64 == base64 &&
					    strcmp(hf->name, hftmp->name) == 0)
						break;
				}
				if (hftmp == NULL)
					hash_insert(&hl, hf, base64);
			}
			break;
		case 'b':
			/* has already been parsed */
			break;
		case 'h':
			ofile = fopen(optarg, "w");
			if (ofile == NULL)
				err(1, "%s", optarg);
			break;
#if !defined(SHA2_ONLY)
		case 'C':
			selective_checklist = optarg;
			break;
		case 'c':
			cflag = 1;
			break;
#endif /* !defined(SHA2_ONLY) */
		case 'p':
			pflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 's':
			input_string = optarg;
			break;
		case 't':
			tflag++;
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (ofile == NULL)
		ofile = stdout;

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");

	/* Most arguments are mutually exclusive */
	fl = pflag + (tflag ? 1 : 0) + xflag + cflag + (input_string != NULL);
	if (fl > 1 || (fl && argc && cflag == 0) || (rflag && qflag) ||
	    (selective_checklist != NULL && argc == 0))
		usage();
	if (selective_checklist || cflag) {
		if (TAILQ_FIRST(&hl) != TAILQ_LAST(&hl, hash_list))
			errx(1, "only a single algorithm may be specified "
			    "in -C or -c mode");
	}

	/* No algorithm specified, check the name we were called as. */
	if (TAILQ_EMPTY(&hl)) {
		for (hf = functions; hf->name != NULL; hf++) {
			if (strcasecmp(hf->name, __progname) == 0)
				break;
		}
		if (hf->name == NULL)
			hf = &functions[0];	/* default to cksum */
		hash_insert(&hl, hf, (hf->base64 == -1 ? 0 : bflag));
	}

	if ((rflag || qflag) && !cflag) {
		const int new_style = rflag ? STYLE_CKSUM : STYLE_TERSE;
		TAILQ_FOREACH(hf, &hl, tailq) {
			hf->style = new_style;
		}
	}

#if !defined(SHA2_ONLY)
	if (tflag)
		digest_time(&hl, tflag);
	else if (xflag)
		digest_test(&hl);
	else if (input_string)
		digest_string(input_string, &hl);
	else if (selective_checklist) {
		int i;

		error = digest_filelist(selective_checklist, TAILQ_FIRST(&hl),
		    argc, argv);
		for (i = 0; i < argc; i++) {
			if (argv[i] != NULL) {
				warnx("%s does not exist in %s", argv[i],
				    selective_checklist);
				error++;
			}
		}
	} else if (cflag) {
		if (argc == 0)
			error = digest_filelist("-", TAILQ_FIRST(&hl), 0, NULL);
		else
			while (argc--)
				error += digest_filelist(*argv++,
				    TAILQ_FIRST(&hl), 0, NULL);
	} else
#endif /* !defined(SHA2_ONLY) */
	if (pflag || argc == 0)
		error = digest_file("-", &hl, pflag);
	else
		while (argc--)
			error += digest_file(*argv++, &hl, 0);

	return(error ? EXIT_FAILURE : EXIT_SUCCESS);
}

void
hash_insert(struct hash_list *hl, struct hash_function *hf, int base64)
{
	struct hash_function *hftmp;

	hftmp = malloc(sizeof(*hftmp));
	if (hftmp == NULL)
		err(1, NULL);
	*hftmp = *hf;
	hftmp->base64 = base64;
	TAILQ_INSERT_TAIL(hl, hftmp, tailq);
}

void
digest_end(const struct hash_function *hf, void *ctx, char *buf, size_t bsize,
    int base64)
{
	u_char *digest;

	if (base64 == 1) {
		if ((digest = malloc(hf->digestlen)) == NULL)
			err(1, NULL);
		hf->final(digest, ctx);
		if (b64_ntop(digest, hf->digestlen, buf, bsize) == -1)
			errx(1, "error encoding base64");
		free(digest);
	} else {
		hf->end(ctx, buf);
	}
}

#if !defined(SHA2_ONLY)
void
digest_string(char *string, struct hash_list *hl)
{
	struct hash_function *hf;
	char digest[MAX_DIGEST_LEN + 1];
	union ANY_CTX context;

	TAILQ_FOREACH(hf, hl, tailq) {
		hf->init(&context);
		hf->update(&context, string, strlen(string));
		digest_end(hf, &context, digest, sizeof(digest),
		    hf->base64);
		digest_printstr(hf, string, digest);
	}
}
#endif /* !defined(SHA2_ONLY) */

void
digest_print(const struct hash_function *hf, const char *what,
    const char *digest)
{
	switch (hf->style) {
	case STYLE_MD5:
		(void)fprintf(ofile, "%s (%s) = %s\n", hf->name, what, digest);
		break;
	case STYLE_CKSUM:
		(void)fprintf(ofile, "%s %s\n", digest, what);
		break;
	case STYLE_TERSE:
		(void)fprintf(ofile, "%s\n", digest);
		break;
	}
}

#if !defined(SHA2_ONLY)
void
digest_printstr(const struct hash_function *hf, const char *what,
    const char *digest)
{
	switch (hf->style) {
	case STYLE_MD5:
		(void)fprintf(ofile, "%s (\"%s\") = %s\n", hf->name, what, digest);
		break;
	case STYLE_CKSUM:
		(void)fprintf(ofile, "%s %s\n", digest, what);
		break;
	case STYLE_TERSE:
		(void)fprintf(ofile, "%s\n", digest);
		break;
	}
}
#endif /* !defined(SHA2_ONLY) */

int
digest_file(const char *file, struct hash_list *hl, int echo)
{
	struct hash_function *hf;
	FILE *fp;
	size_t nread;
	u_char data[32 * 1024];
	char digest[MAX_DIGEST_LEN + 1];

	if (strcmp(file, "-") == 0)
		fp = stdin;
	else if ((fp = fopen(file, "r")) == NULL) {
		warn("cannot open %s", file);
		return(1);
	}

	TAILQ_FOREACH(hf, hl, tailq) {
		if ((hf->ctx = malloc(sizeof(union ANY_CTX))) == NULL)
			err(1, NULL);
		hf->init(hf->ctx);
	}
	while ((nread = fread(data, 1UL, sizeof(data), fp)) != 0) {
		if (echo) {
			(void)fwrite(data, nread, 1UL, stdout);
			(void)fflush(stdout);
			if (ferror(stdout))
				err(1, "stdout: write error");
		}
		TAILQ_FOREACH(hf, hl, tailq)
			hf->update(hf->ctx, data, nread);
	}
	if (ferror(fp)) {
		warn("%s: read error", file);
		if (fp != stdin)
			fclose(fp);
		TAILQ_FOREACH(hf, hl, tailq) {
			free(hf->ctx);
			hf->ctx = NULL;
		}
		return(1);
	}
	if (fp != stdin)
		fclose(fp);
	TAILQ_FOREACH(hf, hl, tailq) {
		digest_end(hf, hf->ctx, digest, sizeof(digest), hf->base64);
		free(hf->ctx);
		hf->ctx = NULL;
		if (fp == stdin)
			fprintf(ofile, "%s\n", digest);
		else
			digest_print(hf, file, digest);
	}
	return(0);
}

#if !defined(SHA2_ONLY)
/*
 * Parse through the input file looking for valid lines.
 * If one is found, use this checksum and file as a reference and
 * generate a new checksum against the file on the filesystem.
 * Print out the result of each comparison.
 */
int
digest_filelist(const char *file, struct hash_function *defhash, int selcount,
    char **sel)
{
	int found, base64, error, cmp, i;
	size_t algorithm_max, algorithm_min;
	const char *algorithm;
	char *filename, *checksum, *line, *p, *tmpline;
	char digest[MAX_DIGEST_LEN + 1];
	ssize_t linelen;
	FILE *listfp, *fp;
	size_t len, linesize, nread;
	int *sel_found = NULL;
	u_char data[32 * 1024];
	union ANY_CTX context;
	struct hash_function *hf;

	if (strcmp(file, "-") == 0) {
		listfp = stdin;
	} else if ((listfp = fopen(file, "r")) == NULL) {
		warn("cannot open %s", file);
		return(1);
	}

	if (sel != NULL) {
		sel_found = calloc((size_t)selcount, sizeof(*sel_found));
		if (sel_found == NULL)
			err(1, NULL);
	}

	algorithm_max = algorithm_min = strlen(functions[0].name);
	for (hf = &functions[1]; hf->name != NULL; hf++) {
		len = strlen(hf->name);
		algorithm_max = MAXIMUM(algorithm_max, len);
		algorithm_min = MINIMUM(algorithm_min, len);
	}

	error = found = 0;
	line = NULL;
	linesize = 0;
	while ((linelen = getline(&line, &linesize, listfp)) != -1) {
		tmpline = line;
		base64 = 0;
		if (line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';
		while (isspace((unsigned char)*tmpline))
			tmpline++;

		/*
		 * Crack the line into an algorithm, filename, and checksum.
		 * Lines are of the form:
		 *  ALGORITHM (FILENAME) = CHECKSUM
		 *
		 * Fallback on GNU form:
		 *  CHECKSUM  FILENAME
		 */
		p = strchr(tmpline, ' ');
		if (p != NULL && *(p + 1) == '(') {
			/* BSD form */
			*p = '\0';
			algorithm = tmpline;
			len = strlen(algorithm);
			if (len > algorithm_max || len < algorithm_min)
				continue;

			filename = p + 2;
			p = strrchr(filename, ')');
			if (p == NULL || strncmp(p + 1, " = ", (size_t)3) != 0)
				continue;
			*p = '\0';

			checksum = p + 4;
			p = strpbrk(checksum, " \t\r");
			if (p != NULL)
				*p = '\0';

			/*
			 * Check that the algorithm is one we recognize.
			 */
			for (hf = functions; hf->name != NULL; hf++) {
				if (strcasecmp(algorithm, hf->name) == 0)
					break;
			}
			if (hf->name == NULL || *checksum == '\0')
				continue;
		} else {
			/* could be GNU form */
			if ((hf = defhash) == NULL)
				continue;
			algorithm = hf->name;
			checksum = tmpline;
			if ((p = strchr(checksum, ' ')) == NULL)
				continue;
			if (hf->style == STYLE_CKSUM) {
				if ((p = strchr(p + 1, ' ')) == NULL)
					continue;
			}
			*p++ = '\0';
			while (isspace((unsigned char)*p))
				p++;
			if (*p == '\0')
				continue;
			filename = p;
			p = strpbrk(filename, "\t\r");
			if (p != NULL)
				*p = '\0';
		}

		if (hf->style == STYLE_MD5) {
			/*
			 * Check the length to see if this could be
			 * a valid digest.  If hex, it will be 2x the
			 * size of the binary data.  For base64, we have
			 * to check both with and without the '=' padding.
			 */
			len = strlen(checksum);
			if (len != hf->digestlen * 2) {
				size_t len2;

				if (checksum[len - 1] == '=') {
					/* use padding */
					len2 = 4 * ((hf->digestlen + 2) / 3);
				} else {
					/* no padding */
					len2 = (4 * hf->digestlen + 2) / 3;
				}
				if (len != len2)
					continue;
				base64 = 1;
			}
		}
		found = 1;

		/*
		 * If only a selection of files is wanted, proceed only
		 * if the filename matches one of those in the selection.
		 */
		if (sel != NULL) {
			for (i = 0; i < selcount; i++) {
				if (strcmp(sel[i], filename) == 0) {
					sel_found[i] = 1;
					break;
				}
			}
			if (i == selcount)
				continue;
		}

		if ((fp = fopen(filename, "r")) == NULL) {
			warn("cannot open %s", filename);
			(void)printf("(%s) %s: %s\n", algorithm, filename,
			    (errno == ENOENT ? "MISSING" : "FAILED"));
			error = 1;
			continue;
		}

		hf->init(&context);
		while ((nread = fread(data, 1UL, sizeof(data), fp)) > 0)
			hf->update(&context, data, nread);
		if (ferror(fp)) {
			warn("%s: read error", file);
			error = 1;
			fclose(fp);
			continue;
		}
		fclose(fp);
		digest_end(hf, &context, digest, sizeof(digest), base64);

		if (base64)
			cmp = strncmp(checksum, digest, len);
		else
			cmp = strcasecmp(checksum, digest);
		if (cmp == 0) {
			if (qflag == 0)
				(void)printf("(%s) %s: OK\n", algorithm,
				    filename);
		} else {
			(void)printf("(%s) %s: FAILED\n", algorithm, filename);
			error = 1;
		}
	}
	free(line);
	if (ferror(listfp)) {
		warn("%s: getline", file);
		error = 1;
	}
	if (listfp != stdin)
		fclose(listfp);
	if (!found)
		warnx("%s: no properly formatted checksum lines found", file);
	if (sel_found != NULL) {
		/*
		 * Mark found files by setting them to NULL so that we can
		 * detect files that are missing from the checklist later.
		 */
		for (i = 0; i < selcount; i++) {
			if (sel_found[i])
				sel[i] = NULL;
		}
		free(sel_found);
	}
	return(error || !found);
}

#define TEST_BLOCK_LEN 10000
#define TEST_BLOCK_COUNT 10000

void
digest_time(struct hash_list *hl, int times)
{
	struct hash_function *hf;
	struct rusage start, stop;
	struct timeval res;
	union ANY_CTX context;
	u_int i;
	u_char data[TEST_BLOCK_LEN];
	char digest[MAX_DIGEST_LEN + 1];
	double elapsed;
	int count = TEST_BLOCK_COUNT;
	while (--times > 0 && count < INT_MAX / 10)
		count *= 10;

	TAILQ_FOREACH(hf, hl, tailq) {
		(void)printf("%s time trial.  Processing %d %d-byte blocks...",
		    hf->name, count, TEST_BLOCK_LEN);
		fflush(stdout);

		/* Initialize data based on block number. */
		for (i = 0; i < TEST_BLOCK_LEN; i++)
			data[i] = (u_char)(i & 0xff);

		getrusage(RUSAGE_SELF, &start);
		hf->init(&context);
		for (i = 0; i < count; i++)
			hf->update(&context, data, (size_t)TEST_BLOCK_LEN);
		digest_end(hf, &context, digest, sizeof(digest), hf->base64);
		getrusage(RUSAGE_SELF, &stop);
		timersub(&stop.ru_utime, &start.ru_utime, &res);
		elapsed = (double)res.tv_sec + (double)res.tv_usec / 1000000.0;

		(void)printf("\nDigest = %s\n", digest);
		(void)printf("Time   = %f seconds\n", elapsed);
		(void)printf("Speed  = %f bytes/second\n",
		    (double)TEST_BLOCK_LEN * count / elapsed);
	}
}

void
digest_test(struct hash_list *hl)
{
	struct hash_function *hf;
	union ANY_CTX context;
	int i;
	char digest[MAX_DIGEST_LEN + 1];
	unsigned char buf[1000];
	unsigned const char *test_strings[] = {
		"",
		"a",
		"abc",
		"message digest",
		"abcdefghijklmnopqrstuvwxyz",
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		    "0123456789",
		"12345678901234567890123456789012345678901234567890123456789"
		    "012345678901234567890",
	};

	TAILQ_FOREACH(hf, hl, tailq) {
		(void)printf("%s test suite:\n", hf->name);

		for (i = 0; i < 8; i++) {
			hf->init(&context);
			hf->update(&context, test_strings[i],
			    strlen(test_strings[i]));
			digest_end(hf, &context, digest, sizeof(digest),
			    hf->base64);
			digest_printstr(hf, test_strings[i], digest);
		}

		/* Now simulate a string of a million 'a' characters. */
		memset(buf, 'a', sizeof(buf));
		hf->init(&context);
		for (i = 0; i < 1000; i++)
			hf->update(&context, buf, sizeof(buf));
		digest_end(hf, &context, digest, sizeof(digest), hf->base64);
		digest_print(hf, "one million 'a' characters",
		    digest);
	}
}
#endif /* !defined(SHA2_ONLY) */

void
usage(void)
{
#if !defined(SHA2_ONLY)
	if (strcmp(__progname, "cksum") == 0)
		fprintf(stderr, "usage: %s [-bcpqrtx] [-a algorithms] [-C checklist] "
		    "[-h hashfile]\n"
		    "	[-s string] [file ...]\n",
		    __progname);
	else
#endif /* !defined(SHA2_ONLY) */
		fprintf(stderr, "usage:"
		    "\t%s [-bcpqrtx] [-C checklist] [-h hashfile] [-s string] "
		    "[file ...]\n",
		    __progname);

	exit(EXIT_FAILURE);
}
