/*
** asm/bootinfo-apollo.h -- Apollo-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_APOLLO_H
#define _UAPI_ASM_M68K_BOOTINFO_APOLLO_H


    /*
     *  Apollo-specific tags
     */

#define BI_APOLLO_MODEL		0x8000	/* model (__be32) */


    /*
     *  Apollo models (BI_APOLLO_MODEL)
     */

#define APOLLO_UNKNOWN		0
#define APOLLO_DN3000		1
#define APOLLO_DN3010		2
#define APOLLO_DN3500		3
#define APOLLO_DN4000		4
#define APOLLO_DN4500		5


#endif /* _UAPI_ASM_M68K_BOOTINFO_APOLLO_H */
