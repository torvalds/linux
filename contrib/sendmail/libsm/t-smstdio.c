/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-smstdio.c,v 1.12 2013-11-22 20:51:43 ca Exp $")

#include <sm/io.h>
#include <sm/string.h>
#include <sm/test.h>

int
main(argc, argv)
	int argc;
	char **argv;
{
	FILE *stream;
	SM_FILE_T *fp;
	char buf[128];
	size_t n;
	static char testmsg[] = "hello, world\n";

	sm_test_begin(argc, argv,
		"test sm_io_stdioopen, smiostdin, smiostdout");

	stream = fopen("t-smstdio.1", "w");
	SM_TEST(stream != NULL);

	fp = sm_io_stdioopen(stream, "w");
	SM_TEST(fp != NULL);

	(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s", testmsg);
	sm_io_close(fp, SM_TIME_DEFAULT);

#if 0
	/*
	**  stream should now be closed.  This is a tricky way to test
	**  if it is still open.  Alas, it core dumps on Linux.
	*/

	fprintf(stream, "oops! stream is still open!\n");
	fclose(stream);
#endif

	stream = fopen("t-smstdio.1", "r");
	SM_TEST(stream != NULL);

	fp = sm_io_stdioopen(stream, "r");
	SM_TEST(fp != NULL);

	n = sm_io_read(fp, SM_TIME_DEFAULT, buf, sizeof(buf));
	if (SM_TEST(n == strlen(testmsg)))
	{
		buf[n] = '\0';
		SM_TEST(strcmp(buf, testmsg) == 0);
	}

#if 0

	/*
	**  Copy smiostdin to smiostdout
	**  gotta think some more about how to test smiostdin and smiostdout
	*/

	while ((c = sm_io_getc(smiostdin)) != SM_IO_EOF)
		sm_io_putc(smiostdout, c);
#endif

	return sm_test_end();
}
