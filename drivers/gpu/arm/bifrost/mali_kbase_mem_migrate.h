/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/**
 * DOC: Base kernel page migration implementation.
 */

#define PAGE_STATUS_MASK ((u8)0x7F)
#define PAGE_STATUS_GET(status) (status & PAGE_STATUS_MASK)
#define PAGE_STATUS_SET(status, value) ((status & ~PAGE_STATUS_MASK) | (value & PAGE_STATUS_MASK))
#define PAGE_ISOLATE_SHIFT (7)
#define PAGE_ISOLATE_SET(status, value)                                                            \
	((status & PAGE_STATUS_MASK) | (value << PAGE_ISOLATE_SHIFT))
#define IS_PAGE_ISOLATED(status) ((bool)(status & ~PAGE_STATUS_MASK))

/* Global integer used to determine if module parameter value has been
 * provided and if page migration feature is enabled.
 */
extern int kbase_page_migration_enabled;

/**
 * kbase_alloc_page_metadata - Allocate and initialize page metadata
 * @kbdev:    Pointer to kbase device.
 * @p:        Page to assign metadata to.
 * @dma_addr: DMA address mapped to paged.
 *
 * This will allocate memory for the page's metadata, initialize it and
 * assign a reference to the page's private field. Importantly, once
 * the metadata is set and ready this function will mark the page as
 * movable.
 *
 * Return: true if successful or false otherwise.
 */
bool kbase_alloc_page_metadata(struct kbase_device *kbdev, struct page *p, dma_addr_t dma_addr);

/**
 * kbase_free_page_later - Defer freeing of given page.
 * @kbdev:  Pointer to kbase device
 * @p:      Page to free
 *
 * This will add given page to a list of pages which will be freed at
 * a later time.
 */
void kbase_free_page_later(struct kbase_device *kbdev, struct page *p);

/*
 * kbase_mem_migrate_set_address_space_ops - Set address space operations
 *
 * @kbdev: Pointer to object representing an instance of GPU platform device.
 * @filp:  Pointer to the struct file corresponding to device file
 *         /dev/malixx instance, passed to the file's open method.
 *
 * Assign address space operations to the given file struct @filp and
 * add a reference to @kbdev.
 */
void kbase_mem_migrate_set_address_space_ops(struct kbase_device *kbdev, struct file *const filp);

/*
 * kbase_mem_migrate_init - Initialise kbase page migration
 *
 * @kbdev: Pointer to kbase device
 *
 * Enables page migration by default based on GPU and setup work queue to
 * defer freeing pages during page migration callbacks.
 */
void kbase_mem_migrate_init(struct kbase_device *kbdev);

/*
 * kbase_mem_migrate_term - Terminate kbase page migration
 *
 * @kbdev: Pointer to kbase device
 *
 * This will flush any work left to free pages from page migration
 * and destroy workqueue associated.
 */
void kbase_mem_migrate_term(struct kbase_device *kbdev);
