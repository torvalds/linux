/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_cpp.h
 * Interface for low-level NFP CPP access.
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */
#ifndef __NFP_CPP_H__
#define __NFP_CPP_H__

#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/sizes.h>

#ifndef NFP_SUBSYS
#define NFP_SUBSYS "nfp"
#endif

#define nfp_err(cpp, fmt, args...) \
	dev_err(nfp_cpp_device(cpp)->parent, NFP_SUBSYS ": " fmt, ## args)
#define nfp_warn(cpp, fmt, args...) \
	dev_warn(nfp_cpp_device(cpp)->parent, NFP_SUBSYS ": " fmt, ## args)
#define nfp_info(cpp, fmt, args...) \
	dev_info(nfp_cpp_device(cpp)->parent, NFP_SUBSYS ": " fmt, ## args)
#define nfp_dbg(cpp, fmt, args...) \
	dev_dbg(nfp_cpp_device(cpp)->parent, NFP_SUBSYS ": " fmt, ## args)

#define PCI_64BIT_BAR_COUNT             3

#define NFP_CPP_NUM_TARGETS             16
/* Max size of area it should be safe to request */
#define NFP_CPP_SAFE_AREA_SIZE		SZ_2M

/* NFP_MUTEX_WAIT_* are timeouts in seconds when waiting for a mutex */
#define NFP_MUTEX_WAIT_FIRST_WARN	15
#define NFP_MUTEX_WAIT_NEXT_WARN	5
#define NFP_MUTEX_WAIT_ERROR		60

struct device;

struct nfp_cpp_area;
struct nfp_cpp;
struct resource;

/* Wildcard indicating a CPP read or write action
 *
 * The action used will be either read or write depending on whether a
 * read or write instruction/call is performed on the NFP_CPP_ID.  It
 * is recomended that the RW action is used even if all actions to be
 * performed on a NFP_CPP_ID are known to be only reads or writes.
 * Doing so will in many cases save NFP CPP internal software
 * resources.
 */
#define NFP_CPP_ACTION_RW               32

#define NFP_CPP_TARGET_ID_MASK          0x1f

/**
 * NFP_CPP_ID() - pack target, token, and action into a CPP ID.
 * @target:     NFP CPP target id
 * @action:     NFP CPP action id
 * @token:      NFP CPP token id
 *
 * Create a 32-bit CPP identifier representing the access to be made.
 * These identifiers are used as parameters to other NFP CPP
 * functions.  Some CPP devices may allow wildcard identifiers to be
 * specified.
 *
 * Return:      NFP CPP ID
 */
#define NFP_CPP_ID(target, action, token)			 \
	((((target) & 0x7f) << 24) | (((token)  & 0xff) << 16) | \
	 (((action) & 0xff) <<  8))

/**
 * NFP_CPP_ISLAND_ID() - pack target, token, action, and island into a CPP ID.
 * @target:     NFP CPP target id
 * @action:     NFP CPP action id
 * @token:      NFP CPP token id
 * @island:     NFP CPP island id
 *
 * Create a 32-bit CPP identifier representing the access to be made.
 * These identifiers are used as parameters to other NFP CPP
 * functions.  Some CPP devices may allow wildcard identifiers to be
 * specified.
 *
 * Return:      NFP CPP ID
 */
#define NFP_CPP_ISLAND_ID(target, action, token, island)	 \
	((((target) & 0x7f) << 24) | (((token)  & 0xff) << 16) | \
	 (((action) & 0xff) <<  8) | (((island) & 0xff) << 0))

/**
 * NFP_CPP_ID_TARGET_of() - Return the NFP CPP target of a NFP CPP ID
 * @id:         NFP CPP ID
 *
 * Return:      NFP CPP target
 */
static inline u8 NFP_CPP_ID_TARGET_of(u32 id)
{
	return (id >> 24) & NFP_CPP_TARGET_ID_MASK;
}

/**
 * NFP_CPP_ID_TOKEN_of() - Return the NFP CPP token of a NFP CPP ID
 * @id:         NFP CPP ID
 * Return:      NFP CPP token
 */
static inline u8 NFP_CPP_ID_TOKEN_of(u32 id)
{
	return (id >> 16) & 0xff;
}

/**
 * NFP_CPP_ID_ACTION_of() - Return the NFP CPP action of a NFP CPP ID
 * @id:         NFP CPP ID
 *
 * Return:      NFP CPP action
 */
static inline u8 NFP_CPP_ID_ACTION_of(u32 id)
{
	return (id >> 8) & 0xff;
}

/**
 * NFP_CPP_ID_ISLAND_of() - Return the NFP CPP island of a NFP CPP ID
 * @id: NFP CPP ID
 *
 * Return:      NFP CPP island
 */
static inline u8 NFP_CPP_ID_ISLAND_of(u32 id)
{
	return (id >> 0) & 0xff;
}

/* NFP Interface types - logical interface for this CPP connection
 * 4 bits are reserved for interface type.
 */
#define NFP_CPP_INTERFACE_TYPE_INVALID      0x0
#define NFP_CPP_INTERFACE_TYPE_PCI          0x1
#define NFP_CPP_INTERFACE_TYPE_ARM          0x2
#define NFP_CPP_INTERFACE_TYPE_RPC          0x3
#define NFP_CPP_INTERFACE_TYPE_ILA          0x4

/**
 * NFP_CPP_INTERFACE() - Construct a 16-bit NFP Interface ID
 * @type:       NFP Interface Type
 * @unit:       Unit identifier for the interface type
 * @channel:    Channel identifier for the interface unit
 *
 * Interface IDs consists of 4 bits of interface type,
 * 4 bits of unit identifier, and 8 bits of channel identifier.
 *
 * The NFP Interface ID is used in the implementation of
 * NFP CPP API mutexes, which use the MU Atomic CompareAndWrite
 * operation - hence the limit to 16 bits to be able to
 * use the NFP Interface ID as a lock owner.
 *
 * Return:      Interface ID
 */
#define NFP_CPP_INTERFACE(type, unit, channel)	\
	((((type) & 0xf) << 12) |		\
	 (((unit) & 0xf) <<  8) |		\
	 (((channel) & 0xff) << 0))

/**
 * NFP_CPP_INTERFACE_TYPE_of() - Get the interface type
 * @interface:  NFP Interface ID
 * Return:      NFP Interface ID's type
 */
#define NFP_CPP_INTERFACE_TYPE_of(interface)   (((interface) >> 12) & 0xf)

/**
 * NFP_CPP_INTERFACE_UNIT_of() - Get the interface unit
 * @interface:  NFP Interface ID
 * Return:      NFP Interface ID's unit
 */
#define NFP_CPP_INTERFACE_UNIT_of(interface)   (((interface) >>  8) & 0xf)

/**
 * NFP_CPP_INTERFACE_CHANNEL_of() - Get the interface channel
 * @interface:  NFP Interface ID
 * Return:      NFP Interface ID's channel
 */
#define NFP_CPP_INTERFACE_CHANNEL_of(interface)   (((interface) >>  0) & 0xff)

/* Implemented in nfp_cppcore.c */
void nfp_cpp_free(struct nfp_cpp *cpp);
u32 nfp_cpp_model(struct nfp_cpp *cpp);
u16 nfp_cpp_interface(struct nfp_cpp *cpp);
int nfp_cpp_serial(struct nfp_cpp *cpp, const u8 **serial);

void *nfp_hwinfo_cache(struct nfp_cpp *cpp);
void nfp_hwinfo_cache_set(struct nfp_cpp *cpp, void *val);
void *nfp_rtsym_cache(struct nfp_cpp *cpp);
void nfp_rtsym_cache_set(struct nfp_cpp *cpp, void *val);

void nfp_nffw_cache_flush(struct nfp_cpp *cpp);

struct nfp_cpp_area *nfp_cpp_area_alloc_with_name(struct nfp_cpp *cpp,
						  u32 cpp_id,
						  const char *name,
						  unsigned long long address,
						  unsigned long size);
struct nfp_cpp_area *nfp_cpp_area_alloc(struct nfp_cpp *cpp, u32 cpp_id,
					unsigned long long address,
					unsigned long size);
void nfp_cpp_area_free(struct nfp_cpp_area *area);
int nfp_cpp_area_acquire(struct nfp_cpp_area *area);
int nfp_cpp_area_acquire_nonblocking(struct nfp_cpp_area *area);
void nfp_cpp_area_release(struct nfp_cpp_area *area);
void nfp_cpp_area_release_free(struct nfp_cpp_area *area);
int nfp_cpp_area_read(struct nfp_cpp_area *area, unsigned long offset,
		      void *buffer, size_t length);
int nfp_cpp_area_write(struct nfp_cpp_area *area, unsigned long offset,
		       const void *buffer, size_t length);
int nfp_cpp_area_check_range(struct nfp_cpp_area *area,
			     unsigned long long offset, unsigned long size);
const char *nfp_cpp_area_name(struct nfp_cpp_area *cpp_area);
void *nfp_cpp_area_priv(struct nfp_cpp_area *cpp_area);
struct nfp_cpp *nfp_cpp_area_cpp(struct nfp_cpp_area *cpp_area);
struct resource *nfp_cpp_area_resource(struct nfp_cpp_area *area);
phys_addr_t nfp_cpp_area_phys(struct nfp_cpp_area *area);
void __iomem *nfp_cpp_area_iomem(struct nfp_cpp_area *area);

int nfp_cpp_area_readl(struct nfp_cpp_area *area, unsigned long offset,
		       u32 *value);
int nfp_cpp_area_writel(struct nfp_cpp_area *area, unsigned long offset,
			u32 value);
int nfp_cpp_area_readq(struct nfp_cpp_area *area, unsigned long offset,
		       u64 *value);
int nfp_cpp_area_writeq(struct nfp_cpp_area *area, unsigned long offset,
			u64 value);
int nfp_cpp_area_fill(struct nfp_cpp_area *area, unsigned long offset,
		      u32 value, size_t length);

int nfp_xpb_readl(struct nfp_cpp *cpp, u32 xpb_tgt, u32 *value);
int nfp_xpb_writel(struct nfp_cpp *cpp, u32 xpb_tgt, u32 value);
int nfp_xpb_writelm(struct nfp_cpp *cpp, u32 xpb_tgt, u32 mask, u32 value);

/* Implemented in nfp_cpplib.c */
int nfp_cpp_read(struct nfp_cpp *cpp, u32 cpp_id,
		 unsigned long long address, void *kernel_vaddr, size_t length);
int nfp_cpp_write(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, const void *kernel_vaddr,
		  size_t length);
int nfp_cpp_readl(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u32 *value);
int nfp_cpp_writel(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u32 value);
int nfp_cpp_readq(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u64 *value);
int nfp_cpp_writeq(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u64 value);

struct nfp_cpp_mutex;

int nfp_cpp_mutex_init(struct nfp_cpp *cpp, int target,
		       unsigned long long address, u32 key_id);
struct nfp_cpp_mutex *nfp_cpp_mutex_alloc(struct nfp_cpp *cpp, int target,
					  unsigned long long address,
					  u32 key_id);
void nfp_cpp_mutex_free(struct nfp_cpp_mutex *mutex);
int nfp_cpp_mutex_lock(struct nfp_cpp_mutex *mutex);
int nfp_cpp_mutex_unlock(struct nfp_cpp_mutex *mutex);
int nfp_cpp_mutex_trylock(struct nfp_cpp_mutex *mutex);

/**
 * nfp_cppcore_pcie_unit() - Get PCI Unit of a CPP handle
 * @cpp:	CPP handle
 *
 * Return: PCI unit for the NFP CPP handle
 */
static inline u8 nfp_cppcore_pcie_unit(struct nfp_cpp *cpp)
{
	return NFP_CPP_INTERFACE_UNIT_of(nfp_cpp_interface(cpp));
}

struct nfp_cpp_explicit;

struct nfp_cpp_explicit_command {
	u32 cpp_id;
	u16 data_ref;
	u8  data_master;
	u8  len;
	u8  byte_mask;
	u8  signal_master;
	u8  signal_ref;
	u8  posted;
	u8  siga;
	u8  sigb;
	s8   siga_mode;
	s8   sigb_mode;
};

#define NFP_SERIAL_LEN		6

/**
 * struct nfp_cpp_operations - NFP CPP operations structure
 * @area_priv_size:     Size of the nfp_cpp_area private data
 * @owner:              Owner module
 * @init:               Initialize the NFP CPP bus
 * @free:               Free the bus
 * @read_serial:	Read serial number to memory provided
 * @get_interface:	Return CPP interface
 * @area_init:          Initialize a new NFP CPP area (not serialized)
 * @area_cleanup:       Clean up a NFP CPP area (not serialized)
 * @area_acquire:       Acquire the NFP CPP area (serialized)
 * @area_release:       Release area (serialized)
 * @area_resource:      Get resource range of area (not serialized)
 * @area_phys:          Get physical address of area (not serialized)
 * @area_iomem:         Get iomem of area (not serialized)
 * @area_read:          Perform a read from a NFP CPP area (serialized)
 * @area_write:         Perform a write to a NFP CPP area (serialized)
 * @explicit_priv_size: Size of an explicit's private area
 * @explicit_acquire:   Acquire an explicit area
 * @explicit_release:   Release an explicit area
 * @explicit_put:       Write data to send
 * @explicit_get:       Read data received
 * @explicit_do:        Perform the transaction
 */
struct nfp_cpp_operations {
	size_t area_priv_size;
	struct module *owner;

	int (*init)(struct nfp_cpp *cpp);
	void (*free)(struct nfp_cpp *cpp);

	void (*read_serial)(struct device *dev, u8 *serial);
	u16 (*get_interface)(struct device *dev);

	int (*area_init)(struct nfp_cpp_area *area,
			 u32 dest, unsigned long long address,
			 unsigned long size);
	void (*area_cleanup)(struct nfp_cpp_area *area);
	int (*area_acquire)(struct nfp_cpp_area *area);
	void (*area_release)(struct nfp_cpp_area *area);
	struct resource *(*area_resource)(struct nfp_cpp_area *area);
	phys_addr_t (*area_phys)(struct nfp_cpp_area *area);
	void __iomem *(*area_iomem)(struct nfp_cpp_area *area);
	int (*area_read)(struct nfp_cpp_area *area, void *kernel_vaddr,
			 unsigned long offset, unsigned int length);
	int (*area_write)(struct nfp_cpp_area *area, const void *kernel_vaddr,
			  unsigned long offset, unsigned int length);

	size_t explicit_priv_size;
	int (*explicit_acquire)(struct nfp_cpp_explicit *expl);
	void (*explicit_release)(struct nfp_cpp_explicit *expl);
	int (*explicit_put)(struct nfp_cpp_explicit *expl,
			    const void *buff, size_t len);
	int (*explicit_get)(struct nfp_cpp_explicit *expl,
			    void *buff, size_t len);
	int (*explicit_do)(struct nfp_cpp_explicit *expl,
			   const struct nfp_cpp_explicit_command *cmd,
			   u64 address);
};

struct nfp_cpp *
nfp_cpp_from_operations(const struct nfp_cpp_operations *ops,
			struct device *parent, void *priv);
void *nfp_cpp_priv(struct nfp_cpp *priv);

int nfp_cpp_area_cache_add(struct nfp_cpp *cpp, size_t size);

/* The following section contains extensions to the
 * NFP CPP API, to be used in a Linux kernel-space context.
 */

/* Use this channel ID for multiple virtual channel interfaces
 * (ie ARM and PCIe) when setting up the interface field.
 */
#define NFP_CPP_INTERFACE_CHANNEL_PEROPENER	255
struct device *nfp_cpp_device(struct nfp_cpp *cpp);

/* Return code masks for nfp_cpp_explicit_do()
 */
#define NFP_SIGNAL_MASK_A	BIT(0)	/* Signal A fired */
#define NFP_SIGNAL_MASK_B	BIT(1)	/* Signal B fired */

enum nfp_cpp_explicit_signal_mode {
	NFP_SIGNAL_NONE = 0,
	NFP_SIGNAL_PUSH = 1,
	NFP_SIGNAL_PUSH_OPTIONAL = -1,
	NFP_SIGNAL_PULL = 2,
	NFP_SIGNAL_PULL_OPTIONAL = -2,
};

struct nfp_cpp_explicit *nfp_cpp_explicit_acquire(struct nfp_cpp *cpp);
int nfp_cpp_explicit_set_target(struct nfp_cpp_explicit *expl, u32 cpp_id,
				u8 len, u8 mask);
int nfp_cpp_explicit_set_data(struct nfp_cpp_explicit *expl,
			      u8 data_master, u16 data_ref);
int nfp_cpp_explicit_set_signal(struct nfp_cpp_explicit *expl,
				u8 signal_master, u8 signal_ref);
int nfp_cpp_explicit_set_posted(struct nfp_cpp_explicit *expl, int posted,
				u8 siga,
				enum nfp_cpp_explicit_signal_mode siga_mode,
				u8 sigb,
				enum nfp_cpp_explicit_signal_mode sigb_mode);
int nfp_cpp_explicit_put(struct nfp_cpp_explicit *expl,
			 const void *buff, size_t len);
int nfp_cpp_explicit_do(struct nfp_cpp_explicit *expl, u64 address);
int nfp_cpp_explicit_get(struct nfp_cpp_explicit *expl, void *buff, size_t len);
void nfp_cpp_explicit_release(struct nfp_cpp_explicit *expl);
struct nfp_cpp *nfp_cpp_explicit_cpp(struct nfp_cpp_explicit *expl);
void *nfp_cpp_explicit_priv(struct nfp_cpp_explicit *cpp_explicit);

/* Implemented in nfp_cpplib.c */

int nfp_cpp_model_autodetect(struct nfp_cpp *cpp, u32 *model);

int nfp_cpp_explicit_read(struct nfp_cpp *cpp, u32 cpp_id,
			  u64 addr, void *buff, size_t len,
			  int width_read);

int nfp_cpp_explicit_write(struct nfp_cpp *cpp, u32 cpp_id,
			   u64 addr, const void *buff, size_t len,
			   int width_write);

#endif /* !__NFP_CPP_H__ */
