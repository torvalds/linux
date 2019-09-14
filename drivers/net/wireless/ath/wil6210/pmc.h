/*
 * Copyright (c) 2012-2015 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>

#define PCM_DATA_INVALID_DW_VAL (0xB0BA0000)

void wil_pmc_init(struct wil6210_priv *wil);
void wil_pmc_alloc(struct wil6210_priv *wil,
		   int num_descriptors, int descriptor_size);
void wil_pmc_free(struct wil6210_priv *wil, int send_pmc_cmd);
int wil_pmc_last_cmd_status(struct wil6210_priv *wil);
ssize_t wil_pmc_read(struct file *, char __user *, size_t, loff_t *);
loff_t wil_pmc_llseek(struct file *filp, loff_t off, int whence);
int wil_pmcring_read(struct seq_file *s, void *data);
