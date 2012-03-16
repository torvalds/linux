/*
 * NVRAM variable manipulation
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmnvram.h 288000 2011-10-05 19:05:16Z $
 */

#ifndef _bcmnvram_h_
#define _bcmnvram_h_

#ifndef _LANGUAGE_ASSEMBLY

#include <typedefs.h>
#include <bcmdefs.h>

struct nvram_header {
	uint32 magic;
	uint32 len;
	uint32 crc_ver_init;	
	uint32 config_refresh;	
	uint32 config_ncdl;	
};

struct nvram_tuple {
	char *name;
	char *value;
	struct nvram_tuple *next;
};


extern char *nvram_default_get(const char *name);


extern int nvram_init(void *sih);


extern int nvram_append(void *si, char *vars, uint varsz);

extern void nvram_get_global_vars(char **varlst, uint *varsz);



extern int nvram_reset(void *sih);


extern void nvram_exit(void *sih);


extern char * nvram_get(const char *name);


extern int nvram_resetgpio_init(void *sih);


static INLINE char *
nvram_safe_get(const char *name)
{
	char *p = nvram_get(name);
	return p ? p : "";
}


static INLINE int
nvram_match(char *name, char *match)
{
	const char *value = nvram_get(name);
	return (value && !strcmp(value, match));
}


static INLINE int
nvram_invmatch(char *name, char *invmatch)
{
	const char *value = nvram_get(name);
	return (value && strcmp(value, invmatch));
}


extern int nvram_set(const char *name, const char *value);


extern int nvram_unset(const char *name);


extern int nvram_commit(void);


extern int nvram_getall(char *nvram_buf, int count);


uint8 nvram_calc_crc(struct nvram_header * nvh);

#endif 


#define NVRAM_SOFTWARE_VERSION	"1"

#define NVRAM_MAGIC		0x48534C46	
#define NVRAM_CLEAR_MAGIC	0x0
#define NVRAM_INVALID_MAGIC	0xFFFFFFFF
#define NVRAM_VERSION		1
#define NVRAM_HEADER_SIZE	20
#define NVRAM_SPACE		0x8000

#define NVRAM_MAX_VALUE_LEN 255
#define NVRAM_MAX_PARAM_LEN 64

#define NVRAM_CRC_START_POSITION	9 
#define NVRAM_CRC_VER_MASK	0xffffff00 


#define NVRAM_START_COMPRESSED	0x400
#define NVRAM_START		0x1000

#define BCM_JUMBO_NVRAM_DELIMIT '\n'
#define BCM_JUMBO_START "Broadcom Jumbo Nvram file"
#endif 
