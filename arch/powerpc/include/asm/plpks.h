/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 IBM Corporation
 * Author: Nayna Jain <nayna@linux.ibm.com>
 *
 * Platform keystore for pseries LPAR(PLPKS).
 */

#ifndef _ASM_POWERPC_PLPKS_H
#define _ASM_POWERPC_PLPKS_H

#ifdef CONFIG_PSERIES_PLPKS

#include <linux/types.h>
#include <linux/list.h>

// Object policy flags from supported_policies
#define PLPKS_OSSECBOOTAUDIT	PPC_BIT32(1) // OS secure boot must be audit/enforce
#define PLPKS_OSSECBOOTENFORCE	PPC_BIT32(2) // OS secure boot must be enforce
#define PLPKS_PWSET		PPC_BIT32(3) // No access without password set
#define PLPKS_WORLDREADABLE	PPC_BIT32(4) // Readable without authentication
#define PLPKS_IMMUTABLE		PPC_BIT32(5) // Once written, object cannot be removed
#define PLPKS_TRANSIENT		PPC_BIT32(6) // Object does not persist through reboot
#define PLPKS_SIGNEDUPDATE	PPC_BIT32(7) // Object can only be modified by signed updates
#define PLPKS_HVPROVISIONED	PPC_BIT32(28) // Hypervisor has provisioned this object

// Signature algorithm flags from signed_update_algorithms
#define PLPKS_ALG_RSA2048	PPC_BIT(0)
#define PLPKS_ALG_RSA4096	PPC_BIT(1)

// Object label OS metadata flags
#define PLPKS_VAR_LINUX		0x02
#define PLPKS_VAR_COMMON	0x04

// Flags for which consumer owns an object is owned by
#define PLPKS_FW_OWNER			0x1
#define PLPKS_BOOTLOADER_OWNER		0x2
#define PLPKS_OS_OWNER			0x3

// Flags for label metadata fields
#define PLPKS_LABEL_VERSION		0
#define PLPKS_MAX_LABEL_ATTR_SIZE	16
#define PLPKS_MAX_NAME_SIZE		239
#define PLPKS_MAX_DATA_SIZE		4000

// Timeouts for PLPKS operations
#define PLPKS_MAX_TIMEOUT		5000 // msec
#define PLPKS_FLUSH_SLEEP		10 // msec
#define PLPKS_FLUSH_SLEEP_RANGE		400

struct plpks_var {
	char *component;
	u8 *name;
	u8 *data;
	u32 policy;
	u16 namelen;
	u16 datalen;
	u8 os;
};

struct plpks_var_name {
	u8  *name;
	u16 namelen;
};

struct plpks_var_name_list {
	u32 varcount;
	struct plpks_var_name varlist[];
};

/**
 * Updates the authenticated variable. It expects NULL as the component.
 */
int plpks_signed_update_var(struct plpks_var *var, u64 flags);

/**
 * Writes the specified var and its data to PKS.
 * Any caller of PKS driver should present a valid component type for
 * their variable.
 */
int plpks_write_var(struct plpks_var var);

/**
 * Removes the specified var and its data from PKS.
 */
int plpks_remove_var(char *component, u8 varos,
		     struct plpks_var_name vname);

/**
 * Returns the data for the specified os variable.
 *
 * Caller must allocate a buffer in var->data with length in var->datalen.
 * If no buffer is provided, var->datalen will be populated with the object's
 * size.
 */
int plpks_read_os_var(struct plpks_var *var);

/**
 * Returns the data for the specified firmware variable.
 *
 * Caller must allocate a buffer in var->data with length in var->datalen.
 * If no buffer is provided, var->datalen will be populated with the object's
 * size.
 */
int plpks_read_fw_var(struct plpks_var *var);

/**
 * Returns the data for the specified bootloader variable.
 *
 * Caller must allocate a buffer in var->data with length in var->datalen.
 * If no buffer is provided, var->datalen will be populated with the object's
 * size.
 */
int plpks_read_bootloader_var(struct plpks_var *var);

/**
 * Returns if PKS is available on this LPAR.
 */
bool plpks_is_available(void);

/**
 * Returns version of the Platform KeyStore.
 */
u8 plpks_get_version(void);

/**
 * Returns hypervisor storage overhead per object, not including the size of
 * the object or label. Only valid for config version >= 2
 */
u16 plpks_get_objoverhead(void);

/**
 * Returns maximum password size. Must be >= 32 bytes
 */
u16 plpks_get_maxpwsize(void);

/**
 * Returns maximum object size supported by Platform KeyStore.
 */
u16 plpks_get_maxobjectsize(void);

/**
 * Returns maximum object label size supported by Platform KeyStore.
 */
u16 plpks_get_maxobjectlabelsize(void);

/**
 * Returns total size of the configured Platform KeyStore.
 */
u32 plpks_get_totalsize(void);

/**
 * Returns used space from the total size of the Platform KeyStore.
 */
u32 plpks_get_usedspace(void);

/**
 * Returns bitmask of policies supported by the hypervisor.
 */
u32 plpks_get_supportedpolicies(void);

/**
 * Returns maximum byte size of a single object supported by the hypervisor.
 * Only valid for config version >= 3
 */
u32 plpks_get_maxlargeobjectsize(void);

/**
 * Returns bitmask of signature algorithms supported for signed updates.
 * Only valid for config version >= 3
 */
u64 plpks_get_signedupdatealgorithms(void);

/**
 * Returns the length of the PLPKS password in bytes.
 */
u16 plpks_get_passwordlen(void);

/**
 * Called in early init to retrieve and clear the PLPKS password from the DT.
 */
void plpks_early_init_devtree(void);

/**
 * Populates the FDT with the PLPKS password to prepare for kexec.
 */
int plpks_populate_fdt(void *fdt);
#else // CONFIG_PSERIES_PLPKS
static inline bool plpks_is_available(void) { return false; }
static inline u16 plpks_get_passwordlen(void) { BUILD_BUG(); }
static inline void plpks_early_init_devtree(void) { }
static inline int plpks_populate_fdt(void *fdt) { BUILD_BUG(); }
#endif // CONFIG_PSERIES_PLPKS

#endif // _ASM_POWERPC_PLPKS_H
