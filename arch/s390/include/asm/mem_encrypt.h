/* SPDX-License-Identifier: GPL-2.0 */
#ifndef S390_MEM_ENCRYPT_H__
#define S390_MEM_ENCRYPT_H__

#ifndef __ASSEMBLER__

int set_memory_encrypted(unsigned long vaddr, int numpages);
int set_memory_decrypted(unsigned long vaddr, int numpages);

#endif	/* __ASSEMBLER__ */

#endif	/* S390_MEM_ENCRYPT_H__ */
