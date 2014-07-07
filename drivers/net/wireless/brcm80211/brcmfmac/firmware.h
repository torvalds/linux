/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef BRCMFMAC_FIRMWARE_H
#define BRCMFMAC_FIRMWARE_H

#define BRCMF_FW_REQUEST		0x000F
#define  BRCMF_FW_REQUEST_NVRAM		0x0001
#define BRCMF_FW_REQ_FLAGS		0x00F0
#define  BRCMF_FW_REQ_NV_OPTIONAL	0x0010

void brcmf_fw_nvram_free(void *nvram);
/*
 * Request firmware(s) asynchronously. When the asynchronous request
 * fails it will not use the callback, but call device_release_driver()
 * instead which will call the driver .remove() callback.
 */
int brcmf_fw_get_firmwares(struct device *dev, u16 flags,
			   const char *code, const char *nvram,
			   void (*fw_cb)(struct device *dev,
					 const struct firmware *fw,
					 void *nvram_image, u32 nvram_len));

#endif /* BRCMFMAC_FIRMWARE_H */
