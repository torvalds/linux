/* SPDX-License-Identifier: GPL-2.0-or-later */
/* AFS protocol bits
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */


#define AFSCAPABILITIESMAX 196 /* Maximum number of words in a capability set */

/* AFS3 Fileserver capabilities word 0 */
#define AFS3_VICED_CAPABILITY_ERRORTRANS	0x0001 /* Uses UAE errors */
#define AFS3_VICED_CAPABILITY_64BITFILES	0x0002 /* FetchData64 & StoreData64 supported */
#define AFS3_VICED_CAPABILITY_WRITELOCKACL	0x0004 /* Can lock a file even without lock perm */
#define AFS3_VICED_CAPABILITY_SANEACLS		0x0008 /* ACLs reviewed for sanity - don't use */
