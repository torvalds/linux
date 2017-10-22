/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Christian Borntraeger (cborntra@de.ibm.com),
 */

#ifndef _ASM_S390_CPCMD_H
#define _ASM_S390_CPCMD_H

/*
 * the lowlevel function for cpcmd
 */
int __cpcmd(const char *cmd, char *response, int rlen, int *response_code);

/*
 * cpcmd is the in-kernel interface for issuing CP commands
 *
 * cmd:		null-terminated command string, max 240 characters
 * response:	response buffer for VM's textual response
 * rlen:	size of the response buffer, cpcmd will not exceed this size
 *		but will cap the output, if its too large. Everything that
 *		did not fit into the buffer will be silently dropped
 * response_code: return pointer for VM's error code
 * return value: the size of the response. The caller can check if the buffer
 *		was large enough by comparing the return value and rlen
 * NOTE: If the response buffer is not in real storage, cpcmd can sleep
 */
int cpcmd(const char *cmd, char *response, int rlen, int *response_code);

#endif /* _ASM_S390_CPCMD_H */
