/*
 * arch/shmedia/boot/compressed/cache.c -- simple cache management functions
 *
 * Code extracted from sh-ipl+g, sh-stub.c, which has the copyright:
 *
 *   This is originally based on an m68k software stub written by Glenn
 *   Engel at HP, but has changed quite a bit.
 *
 *   Modifications for the SH by Ben Lee and Steve Chamberlain
 *
****************************************************************************

		THIS SOFTWARE IS NOT COPYRIGHTED

   HP offers the following for use in the public domain.  HP makes no
   warranty with regard to the software or it's performance and the
   user accepts the software "AS IS" with all faults.

   HP DISCLAIMS ANY WARRANTIES, EXPRESS OR IMPLIED, WITH REGARD
   TO THIS SOFTWARE INCLUDING BUT NOT LIMITED TO THE WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

****************************************************************************/

#define CACHE_ENABLE      0
#define CACHE_DISABLE     1

int cache_control(unsigned int command)
{
	volatile unsigned int *p = (volatile unsigned int *) 0x80000000;
	int i;

	for (i = 0; i < (32 * 1024); i += 32) {
		(void *) *p;
		p += (32 / sizeof (int));
	}

	return 0;
}
