/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */
#ifndef _IPA_CMD_H_
#define _IPA_CMD_H_

#include <linux/types.h>

struct gsi_channel;
struct gsi_trans;
struct ipa;
struct ipa_mem;

/**
 * enum ipa_cmd_opcode:	IPA immediate commands
 *
 * @IPA_CMD_IP_V4_FILTER_INIT:	Initialize IPv4 filter table
 * @IPA_CMD_IP_V6_FILTER_INIT:	Initialize IPv6 filter table
 * @IPA_CMD_IP_V4_ROUTING_INIT:	Initialize IPv4 routing table
 * @IPA_CMD_IP_V6_ROUTING_INIT:	Initialize IPv6 routing table
 * @IPA_CMD_HDR_INIT_LOCAL:	Initialize IPA-local header memory
 * @IPA_CMD_REGISTER_WRITE:	Register write performed by IPA
 * @IPA_CMD_IP_PACKET_INIT:	Set up next packet's destination endpoint
 * @IPA_CMD_DMA_SHARED_MEM:	DMA command performed by IPA
 * @IPA_CMD_IP_PACKET_TAG_STATUS: Have next packet generate tag * status
 * @IPA_CMD_NONE:		Special (invalid) "not a command" value
 *
 * All immediate commands are issued using the AP command TX endpoint.
 */
enum ipa_cmd_opcode {
	IPA_CMD_NONE			= 0x0,
	IPA_CMD_IP_V4_FILTER_INIT	= 0x3,
	IPA_CMD_IP_V6_FILTER_INIT	= 0x4,
	IPA_CMD_IP_V4_ROUTING_INIT	= 0x7,
	IPA_CMD_IP_V6_ROUTING_INIT	= 0x8,
	IPA_CMD_HDR_INIT_LOCAL		= 0x9,
	IPA_CMD_REGISTER_WRITE		= 0xc,
	IPA_CMD_IP_PACKET_INIT		= 0x10,
	IPA_CMD_DMA_SHARED_MEM		= 0x13,
	IPA_CMD_IP_PACKET_TAG_STATUS	= 0x14,
};

/**
 * ipa_cmd_table_init_valid() - Validate a memory region holding a table
 * @ipa:	- IPA pointer
 * @mem:	- IPA memory region descriptor
 * @route:	- Whether the region holds a route or filter table
 *
 * Return:	true if region is valid, false otherwise
 */
bool ipa_cmd_table_init_valid(struct ipa *ipa, const struct ipa_mem *mem,
			      bool route);

/**
 * ipa_cmd_pool_init() - initialize command channel pools
 * @channel:	AP->IPA command TX GSI channel pointer
 * @tre_count:	Number of pool elements to allocate
 *
 * Return:	0 if successful, or a negative error code
 */
int ipa_cmd_pool_init(struct gsi_channel *channel, u32 tre_count);

/**
 * ipa_cmd_pool_exit() - Inverse of ipa_cmd_pool_init()
 * @channel:	AP->IPA command TX GSI channel pointer
 */
void ipa_cmd_pool_exit(struct gsi_channel *channel);

/**
 * ipa_cmd_table_init_add() - Add table init command to a transaction
 * @trans:	GSI transaction
 * @opcode:	IPA immediate command opcode
 * @size:	Size of non-hashed routing table memory
 * @offset:	Offset in IPA shared memory of non-hashed routing table memory
 * @addr:	DMA address of non-hashed table data to write
 * @hash_size:	Size of hashed routing table memory
 * @hash_offset: Offset in IPA shared memory of hashed routing table memory
 * @hash_addr:	DMA address of hashed table data to write
 *
 * If hash_size is 0, hash_offset and hash_addr are ignored.
 */
void ipa_cmd_table_init_add(struct gsi_trans *trans, enum ipa_cmd_opcode opcode,
			    u16 size, u32 offset, dma_addr_t addr,
			    u16 hash_size, u32 hash_offset,
			    dma_addr_t hash_addr);

/**
 * ipa_cmd_hdr_init_local_add() - Add a header init command to a transaction
 * @trans:	GSI transaction
 * @offset:	Offset of header memory in IPA local space
 * @size:	Size of header memory
 * @addr:	DMA address of buffer to be written from
 *
 * Defines and fills the location in IPA memory to use for headers.
 */
void ipa_cmd_hdr_init_local_add(struct gsi_trans *trans, u32 offset, u16 size,
				dma_addr_t addr);

/**
 * ipa_cmd_register_write_add() - Add a register write command to a transaction
 * @trans:	GSI transaction
 * @offset:	Offset of register to be written
 * @value:	Value to be written
 * @mask:	Mask of bits in register to update with bits from value
 * @clear_full: Pipeline clear option; true means full pipeline clear
 */
void ipa_cmd_register_write_add(struct gsi_trans *trans, u32 offset, u32 value,
				u32 mask, bool clear_full);

/**
 * ipa_cmd_dma_shared_mem_add() - Add a DMA memory command to a transaction
 * @trans:	GSI transaction
 * @offset:	Offset of IPA memory to be read or written
 * @size:	Number of bytes of memory to be transferred
 * @addr:	DMA address of buffer to be read into or written from
 * @toward_ipa:	true means write to IPA memory; false means read
 */
void ipa_cmd_dma_shared_mem_add(struct gsi_trans *trans, u32 offset,
				u16 size, dma_addr_t addr, bool toward_ipa);

/**
 * ipa_cmd_pipeline_clear_add() - Add pipeline clear commands to a transaction
 * @trans:	GSI transaction
 */
void ipa_cmd_pipeline_clear_add(struct gsi_trans *trans);

/**
 * ipa_cmd_pipeline_clear_count() - # commands required to clear pipeline
 *
 * Return:	The number of elements to allocate in a transaction
 *		to hold commands to clear the pipeline
 */
u32 ipa_cmd_pipeline_clear_count(void);

/**
 * ipa_cmd_pipeline_clear_wait() - Wait pipeline clear to complete
 * @ipa:	- IPA pointer
 */
void ipa_cmd_pipeline_clear_wait(struct ipa *ipa);

/**
 * ipa_cmd_trans_alloc() - Allocate a transaction for the command TX endpoint
 * @ipa:	IPA pointer
 * @tre_count:	Number of elements in the transaction
 *
 * Return:	A GSI transaction structure, or a null pointer if all
 *		available transactions are in use
 */
struct gsi_trans *ipa_cmd_trans_alloc(struct ipa *ipa, u32 tre_count);

/**
 * ipa_cmd_init() - Initialize IPA immediate commands
 * @ipa:	- IPA pointer
 *
 * Return:	0 if successful, or a negative error code
 *
 * There is no need for a matching ipa_cmd_exit() function.
 */
int ipa_cmd_init(struct ipa *ipa);

#endif /* _IPA_CMD_H_ */
