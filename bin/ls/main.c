/*	$OpenBSD: main.c,v 1.4 2012/12/04 02:27:00 deraadt Exp $	*/

/*
 * Public domain - no warranty.
 */

int ls_main(int argc, char **argv);

int
main(int argc, char *argv[])
{
	return ls_main(argc, argv);
}
