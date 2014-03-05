/* timskmodutils.c
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include "uniklog.h"
#include "timskmod.h"

#define MYDRVNAME "timskmodutils"

BOOL Debug_Malloc_Enabled = FALSE;

/** Print the hexadecimal contents of a data buffer to a supplied print buffer.
 *  @param dest               the print buffer where text characters will
 *			      be written
 *  @param destSize           the maximum number of bytes that can be written
 *			      to #dest
 *  @param src                the buffer that contains the data that is to be
 *			      hex-dumped
 *  @param srcLen             the number of bytes at #src to be hex-dumped
 *  @param bytesToDumpPerLine output will be formatted such that at most
 *			      this many of the input data bytes will be
 *			      represented on each line of output
 *  @return                   the number of text characters written to #dest
 *                            (not including the trailing '\0' byte)
 *  @ingroup internal
 */
int visor_hexDumpToBuffer(char *dest, int destSize, char *prefix, char *src,
			  int srcLen, int bytesToDumpPerLine)
{
	int i = 0;
	int pos = 0;
	char printable[bytesToDumpPerLine + 1];
	char hex[(bytesToDumpPerLine * 3) + 1];
	char *line = NULL;
	int linesize = 1000;
	int linelen = 0;
	int currentlen = 0;
	char emptystring[] = "";
	char *pfx = prefix;
	int baseaddr = 0;
	int rc = 0;

	line = vmalloc(linesize);
	if (line == NULL)
		RETINT(currentlen);

	if (pfx == NULL || (strlen(pfx) > 50))
		pfx = emptystring;
	memset(hex, ' ', bytesToDumpPerLine * 3);
	hex[bytesToDumpPerLine * 3] = '\0';
	memset(printable, ' ', bytesToDumpPerLine);
	printable[bytesToDumpPerLine] = '\0';
	if (destSize > 0)
		dest[0] = '\0';

	for (i = 0; i < srcLen; i++) {
		pos = i % bytesToDumpPerLine;
		if ((pos == 0) && (i > 0)) {
			hex[bytesToDumpPerLine*3] = '\0';
			linelen = sprintf(line, "%s%-6.6x %s %s\n", pfx,
					  baseaddr, hex, printable);
			if ((currentlen) + (linelen) >= destSize)
				RETINT(currentlen);
			strcat(dest, line);
			currentlen += linelen;
			memset(hex, ' ', bytesToDumpPerLine * 3);
			memset(printable, ' ', bytesToDumpPerLine);
			baseaddr = i;
		}
		sprintf(hex + (pos * 3), "%-2.2x ", (uint8_t)(src[i]));
		*(hex + (pos * 3) + 3) = ' ';  /* get rid of null */
		if (((uint8_t)(src[i]) >= ' ') && (uint8_t)(src[i]) < 127)
			printable[pos] = src[i];
		else
			printable[pos] = '.';
	}
	pos = i%bytesToDumpPerLine;
	if (i > 0) {
		hex[bytesToDumpPerLine * 3] = '\0';
		linelen = sprintf(line, "%s%-6.6x %s %s\n",
				  pfx, baseaddr, hex, printable);
		if ((currentlen) + (linelen) >= destSize)
			RETINT(currentlen);
		strcat(dest, line);
		currentlen += linelen;
	}
	RETINT(currentlen);

Away:
	if (line)
		vfree(line);
	return rc;
}
EXPORT_SYMBOL_GPL(visor_hexDumpToBuffer);


/** Callers to interfaces that set __GFP_NORETRY flag below
 *  must check for a NULL (error) result as we are telling the
 *  kernel interface that it is okay to fail.
 */

void *kmalloc_kernel(size_t siz)
{
	return kmalloc(siz, GFP_KERNEL | __GFP_NORETRY);
}

/*  Use these handy-dandy seq_file_xxx functions if you want to call some
 *  functions that write stuff into a seq_file, but you actually just want
 *  to dump that output into a buffer.  Use them as follows:
 *  - call visor_seq_file_new_buffer to create the seq_file (you supply the buf)
 *  - call whatever functions you want that take a seq_file as an argument
 *    (the buf you supplied will get the output data)
 *  - call visor_seq_file_done_buffer to dispose of your seq_file
 */
struct seq_file *visor_seq_file_new_buffer(void *buf, size_t buf_size)
{
	struct seq_file *rc = NULL;
	struct seq_file *m = kmalloc_kernel(sizeof(struct seq_file));

	if (m == NULL)
		RETPTR(NULL);
	memset(m, 0, sizeof(struct seq_file));
	m->buf = buf;
	m->size = buf_size;
	RETPTR(m);
Away:
	if (rc == NULL) {
		visor_seq_file_done_buffer(m);
		m = NULL;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(visor_seq_file_new_buffer);



void visor_seq_file_done_buffer(struct seq_file *m)
{
	if (!m)
		return;
	kfree(m);
}
EXPORT_SYMBOL_GPL(visor_seq_file_done_buffer);
