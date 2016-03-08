/*
 * NVRAM variable manipulation
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmnvram.h 514727 2014-11-12 03:02:48Z $
 */

#ifndef _bcmnvram_h_
#define _bcmnvram_h_

#ifndef _LANGUAGE_ASSEMBLY

#include <typedefs.h>
#include <bcmdefs.h>

struct nvram_header {
	uint32 magic;
	uint32 len;
	uint32 crc_ver_init;	/* 0:7 crc, 8:15 ver, 16:31 sdram_init */
	uint32 config_refresh;	/* 0:15 sdram_config, 16:31 sdram_refresh */
	uint32 config_ncdl;	/* ncdl values for memc */
};

struct nvram_tuple {
	char *name;
	char *value;
	struct nvram_tuple *next;
};

/*
 * Get default value for an NVRAM variable
 */
extern char *nvram_default_get(const char *name);
/*
 * validate/restore all per-interface related variables
 */
extern void nvram_validate_all(char *prefix, bool restore);

/*
 * restore specific per-interface variable
 */
extern void nvram_restore_var(char *prefix, char *name);

/*
 * Initialize NVRAM access. May be unnecessary or undefined on certain
 * platforms.
 */
extern int nvram_init(void *sih);
extern int nvram_deinit(void *sih);


/*
 * Append a chunk of nvram variables to the global list
 */
extern int nvram_append(void *si, char *vars, uint varsz);

extern void nvram_get_global_vars(char **varlst, uint *varsz);


/*
 * Check for reset button press for restoring factory defaults.
 */
extern int nvram_reset(void *sih);

/*
 * Disable NVRAM access. May be unnecessary or undefined on certain
 * platforms.
 */
extern void nvram_exit(void *sih);

/*
 * Get the value of an NVRAM variable. The pointer returned may be
 * invalid after a set.
 * @param	name	name of variable to get
 * @return	value of variable or NULL if undefined
 */
extern char * nvram_get(const char *name);

/*
 * Get the value of an NVRAM variable. The pointer returned may be
 * invalid after a set.
 * @param	name	name of variable to get
 * @param	bit	bit value to get
 * @return	value of variable or NULL if undefined
 */
extern char * nvram_get_bitflag(const char *name, const int bit);

/*
 * Read the reset GPIO value from the nvram and set the GPIO
 * as input
 */
extern int nvram_resetgpio_init(void *sih);

/*
 * Get the value of an NVRAM variable.
 * @param	name	name of variable to get
 * @return	value of variable or NUL if undefined
 */
static INLINE char *
nvram_safe_get(const char *name)
{
	char *p = nvram_get(name);
	return p ? p : "";
}

/*
 * Match an NVRAM variable.
 * @param	name	name of variable to match
 * @param	match	value to compare against value of variable
 * @return	TRUE if variable is defined and its value is string equal
 *		to match or FALSE otherwise
 */
static INLINE int
nvram_match(const char *name, const char *match)
{
	const char *value = nvram_get(name);
	return (value && !strcmp(value, match));
}

/*
 * Match an NVRAM variable.
 * @param	name	name of variable to match
 * @param	bit	bit value to get
 * @param	match	value to compare against value of variable
 * @return	TRUE if variable is defined and its value is string equal
 *		to match or FALSE otherwise
 */
static INLINE int
nvram_match_bitflag(const char *name, const int bit, const char *match)
{
	const char *value = nvram_get_bitflag(name, bit);
	return (value && !strcmp(value, match));
}

/*
 * Inversely match an NVRAM variable.
 * @param	name	name of variable to match
 * @param	match	value to compare against value of variable
 * @return	TRUE if variable is defined and its value is not string
 *		equal to invmatch or FALSE otherwise
 */
static INLINE int
nvram_invmatch(const char *name, const char *invmatch)
{
	const char *value = nvram_get(name);
	return (value && strcmp(value, invmatch));
}

/*
 * Set the value of an NVRAM variable. The name and value strings are
 * copied into private storage. Pointers to previously set values
 * may become invalid. The new value may be immediately
 * retrieved but will not be permanently stored until a commit.
 * @param	name	name of variable to set
 * @param	value	value of variable
 * @return	0 on success and errno on failure
 */
extern int nvram_set(const char *name, const char *value);

/*
 * Set the value of an NVRAM variable. The name and value strings are
 * copied into private storage. Pointers to previously set values
 * may become invalid. The new value may be immediately
 * retrieved but will not be permanently stored until a commit.
 * @param	name	name of variable to set
 * @param	bit	bit value to set
 * @param	value	value of variable
 * @return	0 on success and errno on failure
 */
extern int nvram_set_bitflag(const char *name, const int bit, const int value);
/*
 * Unset an NVRAM variable. Pointers to previously set values
 * remain valid until a set.
 * @param	name	name of variable to unset
 * @return	0 on success and errno on failure
 * NOTE: use nvram_commit to commit this change to flash.
 */
extern int nvram_unset(const char *name);

/*
 * Commit NVRAM variables to permanent storage. All pointers to values
 * may be invalid after a commit.
 * NVRAM values are undefined after a commit.
 * @param   nvram_corrupt    true to corrupt nvram, false otherwise.
 * @return	0 on success and errno on failure
 */
extern int nvram_commit_internal(bool nvram_corrupt);

/*
 * Commit NVRAM variables to permanent storage. All pointers to values
 * may be invalid after a commit.
 * NVRAM values are undefined after a commit.
 * @return	0 on success and errno on failure
 */
extern int nvram_commit(void);

/*
 * Get all NVRAM variables (format name=value\0 ... \0\0).
 * @param	buf	buffer to store variables
 * @param	count	size of buffer in bytes
 * @return	0 on success and errno on failure
 */
extern int nvram_getall(char *nvram_buf, int count);

/*
 * returns the crc value of the nvram
 * @param	nvh	nvram header pointer
 */
uint8 nvram_calc_crc(struct nvram_header * nvh);

extern int nvram_space;
#endif /* _LANGUAGE_ASSEMBLY */

/* The NVRAM version number stored as an NVRAM variable */
#define NVRAM_SOFTWARE_VERSION	"1"

#define NVRAM_MAGIC		0x48534C46	/* 'FLSH' */
#define NVRAM_CLEAR_MAGIC	0x0
#define NVRAM_INVALID_MAGIC	0xFFFFFFFF
#define NVRAM_VERSION		1
#define NVRAM_HEADER_SIZE	20
/* This definition is for precommit staging, and will be removed */
#define NVRAM_SPACE		0x8000
/* For CFE builds this gets passed in thru the makefile */
#ifndef MAX_NVRAM_SPACE
#define MAX_NVRAM_SPACE		0x10000
#endif
#define DEF_NVRAM_SPACE		0x8000
#define ROM_ENVRAM_SPACE	0x1000
#define NVRAM_LZMA_MAGIC	0x4c5a4d41	/* 'LZMA' */

#define NVRAM_MAX_VALUE_LEN 255
#define NVRAM_MAX_PARAM_LEN 64

#define NVRAM_CRC_START_POSITION	9 /* magic, len, crc8 to be skipped */
#define NVRAM_CRC_VER_MASK	0xffffff00 /* for crc_ver_init */

/* Offsets to embedded nvram area */
#define NVRAM_START_COMPRESSED	0x400
#define NVRAM_START		0x1000

#define BCM_JUMBO_NVRAM_DELIMIT '\n'
#define BCM_JUMBO_START "Broadcom Jumbo Nvram file"


#if (defined(FAILSAFE_UPGRADE) || defined(CONFIG_FAILSAFE_UPGRADE) || \
	defined(__CONFIG_FAILSAFE_UPGRADE_SUPPORT__))
#define IMAGE_SIZE "image_size"
#define BOOTPARTITION "bootpartition"
#define IMAGE_BOOT BOOTPARTITION
#define PARTIALBOOTS "partialboots"
#define MAXPARTIALBOOTS "maxpartialboots"
#define IMAGE_1ST_FLASH_TRX "flash0.trx"
#define IMAGE_1ST_FLASH_OS "flash0.os"
#define IMAGE_2ND_FLASH_TRX "flash0.trx2"
#define IMAGE_2ND_FLASH_OS "flash0.os2"
#define IMAGE_FIRST_OFFSET "image_first_offset"
#define IMAGE_SECOND_OFFSET "image_second_offset"
#define LINUX_FIRST "linux"
#define LINUX_SECOND "linux2"
#endif

#if (defined(DUAL_IMAGE) || defined(CONFIG_DUAL_IMAGE) || \
	defined(__CONFIG_DUAL_IMAGE_FLASH_SUPPORT__))
/* Shared by all: CFE, Linux Kernel, and Ap */
#define IMAGE_BOOT "image_boot"
#define BOOTPARTITION IMAGE_BOOT
/* CFE variables */
#define IMAGE_1ST_FLASH_TRX "flash0.trx"
#define IMAGE_1ST_FLASH_OS "flash0.os"
#define IMAGE_2ND_FLASH_TRX "flash0.trx2"
#define IMAGE_2ND_FLASH_OS "flash0.os2"
#define IMAGE_SIZE "image_size"

/* CFE and Linux Kernel shared variables */
#define IMAGE_FIRST_OFFSET "image_first_offset"
#define IMAGE_SECOND_OFFSET "image_second_offset"

/* Linux application variables */
#define LINUX_FIRST "linux"
#define LINUX_SECOND "linux2"
#define POLICY_TOGGLE "toggle"
#define LINUX_PART_TO_FLASH "linux_to_flash"
#define LINUX_FLASH_POLICY "linux_flash_policy"

#endif /* defined(DUAL_IMAGE||CONFIG_DUAL_IMAGE)||__CONFIG_DUAL_IMAGE_FLASH_SUPPORT__ */

#endif /* _bcmnvram_h_ */
