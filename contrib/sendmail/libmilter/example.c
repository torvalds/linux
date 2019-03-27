/*
 *  Copyright (c) 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 * $Id: example.c,v 8.5 2013-11-22 20:51:36 ca Exp $
 */

/*
**  A trivial example filter that logs all email to a file.
**  This milter also has some callbacks which it does not really use,
**  but they are defined to serve as an example.
*/

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "libmilter/mfapi.h"
#include "libmilter/mfdef.h"

#ifndef true
# define false	0
# define true	1
#endif /* ! true */

struct mlfiPriv
{
	char	*mlfi_fname;
	FILE	*mlfi_fp;
};

#define MLFIPRIV	((struct mlfiPriv *) smfi_getpriv(ctx))

static unsigned long mta_caps = 0;

sfsistat
mlfi_cleanup(ctx, ok)
	SMFICTX *ctx;
	bool ok;
{
	sfsistat rstat = SMFIS_CONTINUE;
	struct mlfiPriv *priv = MLFIPRIV;
	char *p;
	char host[512];
	char hbuf[1024];

	if (priv == NULL)
		return rstat;

	/* close the archive file */
	if (priv->mlfi_fp != NULL && fclose(priv->mlfi_fp) == EOF)
	{
		/* failed; we have to wait until later */
		rstat = SMFIS_TEMPFAIL;
		(void) unlink(priv->mlfi_fname);
	}
	else if (ok)
	{
		/* add a header to the message announcing our presence */
		if (gethostname(host, sizeof host) < 0)
			snprintf(host, sizeof host, "localhost");
		p = strrchr(priv->mlfi_fname, '/');
		if (p == NULL)
			p = priv->mlfi_fname;
		else
			p++;
		snprintf(hbuf, sizeof hbuf, "%s@%s", p, host);
		smfi_addheader(ctx, "X-Archived", hbuf);
	}
	else
	{
		/* message was aborted -- delete the archive file */
		(void) unlink(priv->mlfi_fname);
	}

	/* release private memory */
	free(priv->mlfi_fname);
	free(priv);
	smfi_setpriv(ctx, NULL);

	/* return status */
	return rstat;
}


sfsistat
mlfi_envfrom(ctx, envfrom)
	SMFICTX *ctx;
	char **envfrom;
{
	struct mlfiPriv *priv;
	int fd = -1;

	/* allocate some private memory */
	priv = malloc(sizeof *priv);
	if (priv == NULL)
	{
		/* can't accept this message right now */
		return SMFIS_TEMPFAIL;
	}
	memset(priv, '\0', sizeof *priv);

	/* open a file to store this message */
	priv->mlfi_fname = strdup("/tmp/msg.XXXXXXXX");
	if (priv->mlfi_fname == NULL)
	{
		free(priv);
		return SMFIS_TEMPFAIL;
	}
	if ((fd = mkstemp(priv->mlfi_fname)) < 0 ||
	    (priv->mlfi_fp = fdopen(fd, "w+")) == NULL)
	{
		if (fd >= 0)
			(void) close(fd);
		free(priv->mlfi_fname);
		free(priv);
		return SMFIS_TEMPFAIL;
	}

	/* save the private data */
	smfi_setpriv(ctx, priv);

	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat
mlfi_header(ctx, headerf, headerv)
	SMFICTX *ctx;
	char *headerf;
	char *headerv;
{
	/* write the header to the log file */
	fprintf(MLFIPRIV->mlfi_fp, "%s: %s\r\n", headerf, headerv);

	/* continue processing */
	return ((mta_caps & SMFIP_NR_HDR) != 0)
		? SMFIS_NOREPLY : SMFIS_CONTINUE;
}

sfsistat
mlfi_eoh(ctx)
	SMFICTX *ctx;
{
	/* output the blank line between the header and the body */
	fprintf(MLFIPRIV->mlfi_fp, "\r\n");

	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat
mlfi_body(ctx, bodyp, bodylen)
	SMFICTX *ctx;
	u_char *bodyp;
	size_t bodylen;
{
	/* output body block to log file */
	if (fwrite(bodyp, bodylen, 1, MLFIPRIV->mlfi_fp) <= 0)
	{
		/* write failed */
		(void) mlfi_cleanup(ctx, false);
		return SMFIS_TEMPFAIL;
	}

	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat
mlfi_eom(ctx)
	SMFICTX *ctx;
{
	return mlfi_cleanup(ctx, true);
}

sfsistat
mlfi_close(ctx)
	SMFICTX *ctx;
{
	return SMFIS_ACCEPT;
}

sfsistat
mlfi_abort(ctx)
	SMFICTX *ctx;
{
	return mlfi_cleanup(ctx, false);
}

sfsistat
mlfi_unknown(ctx, cmd)
	SMFICTX *ctx;
	char *cmd;
{
	return SMFIS_CONTINUE;
}

sfsistat
mlfi_data(ctx)
	SMFICTX *ctx;
{
	return SMFIS_CONTINUE;
}

sfsistat
mlfi_negotiate(ctx, f0, f1, f2, f3, pf0, pf1, pf2, pf3)
	SMFICTX *ctx;
	unsigned long f0;
	unsigned long f1;
	unsigned long f2;
	unsigned long f3;
	unsigned long *pf0;
	unsigned long *pf1;
	unsigned long *pf2;
	unsigned long *pf3;
{
	/* milter actions: add headers */
	*pf0 = SMFIF_ADDHDRS;

	/* milter protocol steps: all but connect, HELO, RCPT */
	*pf1 = SMFIP_NOCONNECT|SMFIP_NOHELO|SMFIP_NORCPT;
	mta_caps = f1;
	if ((mta_caps & SMFIP_NR_HDR) != 0)
		*pf1 |= SMFIP_NR_HDR;
	*pf2 = 0;
	*pf3 = 0;
	return SMFIS_CONTINUE;
}

struct smfiDesc smfilter =
{
	"SampleFilter",	/* filter name */
	SMFI_VERSION,	/* version code -- do not change */
	SMFIF_ADDHDRS,	/* flags */
	NULL,		/* connection info filter */
	NULL,		/* SMTP HELO command filter */
	mlfi_envfrom,	/* envelope sender filter */
	NULL,		/* envelope recipient filter */
	mlfi_header,	/* header filter */
	mlfi_eoh,	/* end of header */
	mlfi_body,	/* body block filter */
	mlfi_eom,	/* end of message */
	mlfi_abort,	/* message aborted */
	mlfi_close,	/* connection cleanup */
	mlfi_unknown,	/* unknown/unimplemented SMTP commands */
	mlfi_data,	/* DATA command filter */
	mlfi_negotiate	/* option negotiation at connection startup */
};

int
main(argc, argv)
	int argc;
	char *argv[];
{
	bool setconn;
	int c;

	setconn = false;

	/* Process command line options */
	while ((c = getopt(argc, argv, "p:")) != -1)
	{
		switch (c)
		{
		  case 'p':
			if (optarg == NULL || *optarg == '\0')
			{
				(void) fprintf(stderr, "Illegal conn: %s\n",
					       optarg);
				exit(EX_USAGE);
			}
			(void) smfi_setconn(optarg);
			setconn = true;
			break;

		}
	}
	if (!setconn)
	{
		fprintf(stderr, "%s: Missing required -p argument\n", argv[0]);
		exit(EX_USAGE);
	}
	if (smfi_register(smfilter) == MI_FAILURE)
	{
		fprintf(stderr, "smfi_register failed\n");
		exit(EX_UNAVAILABLE);
	}
	return smfi_main();
}

