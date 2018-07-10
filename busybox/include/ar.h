/*
 * busybox ar archive data structures
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#ifndef AR_H
#define AR_H

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

struct ar_header {
	char name[16];
	char date[12];
	char uid[6];
	char gid[6];
	char mode[8];
	char size[10];
	char magic[2];
};

#define AR_HEADER_LEN sizeof(struct ar_header)
#define AR_MAGIC      "!<arch>"
#define AR_MAGIC_LEN  7

POP_SAVED_FUNCTION_VISIBILITY

#endif
