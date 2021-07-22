/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _CERT_COMMON_H
#define _CERT_COMMON_H

int load_certificate_list(const u8 cert_list[], const unsigned long list_size,
			  const struct key *keyring);

#endif
