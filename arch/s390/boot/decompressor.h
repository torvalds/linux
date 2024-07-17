/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_DECOMPRESSOR_H
#define BOOT_COMPRESSED_DECOMPRESSOR_H

#ifndef CONFIG_KERNEL_UNCOMPRESSED
unsigned long mem_safe_offset(void);
void deploy_kernel(void *output);
#endif

#endif /* BOOT_COMPRESSED_DECOMPRESSOR_H */
