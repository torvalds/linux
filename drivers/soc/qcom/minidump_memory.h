/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define MD_MEMINFO_PAGES	1
#define MD_SLABINFO_PAGES	8
#ifdef CONFIG_PAGE_OWNER
extern size_t md_pageowner_dump_size;
extern char *md_pageowner_dump_addr;
#endif
#ifdef CONFIG_SLUB_DEBUG
extern size_t md_slabowner_dump_size;
extern char *md_slabowner_dump_addr;
#endif
extern size_t md_dma_buf_info_size;
extern char *md_dma_buf_info_addr;
extern size_t md_dma_buf_procs_size;
extern char *md_dma_buf_procs_addr;

void md_dump_meminfo(struct seq_buf *m);
#ifdef CONFIG_SLUB_DEBUG
void md_dump_slabinfo(struct seq_buf *m);
#else
static inline void md_dump_slabinfo(struct seq_buf *m) {}
#endif
bool md_register_memory_dump(int size, char *name);
bool md_unregister_memory_dump(char *name);
#ifdef CONFIG_PAGE_OWNER
bool is_page_owner_enabled(void);
void md_dump_pageowner(char *addr, size_t dump_size);
void md_debugfs_pageowner(struct dentry *minidump_dir);
#else
static inline bool is_page_owner_enabled(void) { return false; }
static inline void md_dump_pageowner(char *addr, size_t dump_size) {}
static inline void md_debugfs_pageowner(struct dentry *minidump_dir) {}
#endif
#ifdef CONFIG_SLUB_DEBUG
bool is_slub_debug_enabled(void);
void md_dump_slabowner(char *addr, size_t dump_size);
void md_debugfs_slabowner(struct dentry *minidump_dir);
#else
static inline bool is_slub_debug_enabled(void) { return false; }
static inline void md_dump_slabowner(char *addr, size_t dump_size) {}
static inline void md_debugfs_slabowner(struct dentry *minidump_dir) {}
#endif
void md_dma_buf_info(char *m, size_t dump_size);
void md_debugfs_dmabufinfo(struct dentry *minidump_dir);
void md_dma_buf_procs(char *m, size_t dump_size);
void md_debugfs_dmabufprocs(struct dentry *minidump_dir);
int md_minidump_memory_init(void);
