/*
 * ESI service calls.
 *
 * Copyright (c) Copyright 2005-2006 Hewlett-Packard Development Company, L.P.
 * 	Alex Williamson <alex.williamson@hp.com>
 */
#ifndef esi_h
#define esi_h

#include <linux/efi.h>

#define ESI_QUERY			0x00000001
#define ESI_OPEN_HANDLE			0x02000000
#define ESI_CLOSE_HANDLE		0x02000001

enum esi_proc_type {
	ESI_PROC_SERIALIZED,	/* calls need to be serialized */
	ESI_PROC_MP_SAFE,	/* MP-safe, but not reentrant */
	ESI_PROC_REENTRANT	/* MP-safe and reentrant */
};

extern struct ia64_sal_retval esi_call_phys (void *, u64 *);
extern int ia64_esi_call(efi_guid_t, struct ia64_sal_retval *,
			 enum esi_proc_type,
			 u64, u64, u64, u64, u64, u64, u64, u64);
extern int ia64_esi_call_phys(efi_guid_t, struct ia64_sal_retval *, u64, u64,
                              u64, u64, u64, u64, u64, u64);

#endif /* esi_h */
