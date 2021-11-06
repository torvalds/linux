/*
 * bcmiov.h
 * Common iovar handling/parsing support - batching, parsing, sub-cmd dispatch etc.
 * To be used in firmware and host apps or dhd - reducing code size,
 * duplication, and maintenance overhead.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _bcmiov_h_
#define _bcmiov_h_

#include <typedefs.h>
#include <bcmutils.h>
#include <wlioctl.h>
#ifdef BCMDRIVER
#include <osl.h>
#else
#include <stddef.h>  /* For size_t */
#endif /* BCMDRIVER */

/* Forward declarations */
typedef uint16 bcm_iov_cmd_id_t;
typedef uint16 bcm_iov_cmd_flags_t;
typedef uint16 bcm_iov_cmd_mflags_t;
typedef struct bcm_iov_cmd_info bcm_iov_cmd_info_t;
typedef struct bcm_iov_cmd_digest bcm_iov_cmd_digest_t;
typedef struct bcm_iov_cmd_tlv_info bcm_iov_cmd_tlv_info_t;
typedef struct bcm_iov_buf bcm_iov_buf_t;
typedef struct bcm_iov_batch_buf bcm_iov_batch_buf_t;
typedef struct bcm_iov_parse_context bcm_iov_parse_context_t;
typedef struct bcm_iov_sub_cmd_context bcm_iov_sub_cmd_context_t;

typedef void* (*bcm_iov_malloc_t)(void* alloc_ctx, size_t len);
typedef void (*bcm_iov_free_t)(void* alloc_ctx, void *buf, size_t len);

typedef uint8 bcm_iov_tlp_data_type_t;
typedef struct bcm_iov_tlp bcm_iov_tlp_t;
typedef struct bcm_iov_tlp_node bcm_iov_tlp_node_t;
typedef struct bcm_iov_batch_subcmd bcm_iov_batch_subcmd_t;

/*
 * iov validation handler - All the common checks that are required
 * for processing of iovars for any given command.
 */
typedef int (*bcm_iov_cmd_validate_t)(const bcm_iov_cmd_digest_t *dig,
	uint32 actionid, const uint8* ibuf, size_t ilen, uint8 *obuf, size_t *olen);

/* iov get handler - process subcommand specific input and return output.
 * input and output may overlap, so the callee needs to check if
 * that is supported. For xtlv data a tlv digest is provided to make
 * parsing simpler. Output tlvs may be packed into output buffer using
 * bcm xtlv support. olen is input/output parameter. On input contains
 * max available obuf length and callee must fill the correct length
 * to represent the length of output returned.
 */
typedef int (*bcm_iov_cmd_get_t)(const bcm_iov_cmd_digest_t *dig,
	const uint8* ibuf, size_t ilen, uint8 *obuf, size_t *olen);

/* iov set handler - process subcommand specific input and return output
 * input and output may overlap, so the callee needs to check if
 * that is supported. olen is input/output parameter. On input contains
 * max available obuf length and callee must fill the correct length
 * to represent the length of output returned.
 */
typedef int (*bcm_iov_cmd_set_t)(const bcm_iov_cmd_digest_t *dig,
	const uint8* ibuf, size_t ilen, uint8 *obuf, size_t *olen);

/* iov (sub-cmd) batch - a vector of commands. count can be zero
 * to support a version query. Each command is a tlv - whose data
 * portion may have an optional return status, followed by a fixed
 * length data header, optionally followed by tlvs.
 *    cmd = type|length|<status|options>[header][tlvs]
 */

/*
 * Batch sub-commands have status length included in the
 * response length packed in TLV.
 */
#define BCM_IOV_STATUS_LEN sizeof(uint32)

/* batch version is indicated by setting high bit. */
#define BCM_IOV_BATCH_MASK	0x8000

/*
 * Batched commands will have the following memory layout
 * +--------+---------+--------+-------+
 * |version |count    | is_set |sub-cmd|
 * +--------+---------+--------+-------+
 * version >= 0x8000
 * count = number of sub-commands encoded in the iov buf
 * sub-cmd one or more sub-commands for processing
 * Where sub-cmd is padded byte buffer with memory layout as follows
 * +--------+---------+-----------------------+-------------+------
 * |cmd-id  |length   |IN(options) OUT(status)|command data |......
 * +--------+---------+-----------------------+-------------+------
 * cmd-id =sub-command ID
 * length = length of this sub-command
 * IN(options) = On input processing options/flags for this command
 * OUT(status) on output processing status for this command
 * command data = encapsulated IOVAR data as a single structure or packed TLVs for each
 * individual sub-command.
 */
struct bcm_iov_batch_subcmd {
	uint16 id;
	uint16 len;
	union {
		uint32 options;
		uint32 status;
	} u;
	uint8 data[1];
};

struct bcm_iov_batch_buf {
	uint16 version;
	uint8 count;
	uint8 is_set; /* to differentiate set or get */
	struct bcm_iov_batch_subcmd cmds[0];
};

/* non-batched command version = major|minor w/ major <= 127 */
struct bcm_iov_buf {
	uint16 version;
	uint16 len;
	bcm_iov_cmd_id_t id;
	uint16 data[1]; /* 32 bit alignment may be repurposed by the command */
	/* command specific data follows */
};

/* iov options flags */
enum {
	BCM_IOV_CMD_OPT_ALIGN_NONE = 0x0000,
	BCM_IOV_CMD_OPT_ALIGN32 = 0x0001,
	BCM_IOV_CMD_OPT_TERMINATE_SUB_CMDS = 0x0002
};

/* iov command flags */
enum {
	BCM_IOV_CMD_FLAG_NONE = 0,
	BCM_IOV_CMD_FLAG_STATUS_PRESENT = (1 << 0), /* status present at data start - output only */
	BCM_IOV_CMD_FLAG_XTLV_DATA = (1 << 1),  /* data is a set of xtlvs */
	BCM_IOV_CMD_FLAG_HDR_IN_LEN = (1 << 2), /* length starts at version - non-bacthed only */
	BCM_IOV_CMD_FLAG_NOPAD = (1 << 3) /* No padding needed after iov_buf */
};

/* information about the command, xtlv options and xtlvs_off are meaningful
 * only if XTLV_DATA cmd flag is selected
 */
struct bcm_iov_cmd_info {
	bcm_iov_cmd_id_t	cmd;		/* the (sub)command - module specific */
	bcm_iov_cmd_flags_t	flags;		/* checked by bcmiov but set by module */
	bcm_iov_cmd_mflags_t	mflags;		/* owned and checked by module */
	bcm_xtlv_opts_t		xtlv_opts;
	bcm_iov_cmd_validate_t	validate_h;	/* command validation handler */
	bcm_iov_cmd_get_t	get_h;
	bcm_iov_cmd_set_t	set_h;
	uint16			xtlvs_off;	/* offset to beginning of xtlvs in cmd data */
	uint16			min_len_set;
	uint16			max_len_set;
	uint16			min_len_get;
	uint16			max_len_get;
};

/* tlv digest to support parsing of xtlvs for commands w/ tlv data; the tlv
 * digest is available in the handler for the command. The count and order in
 * which tlvs appear in the digest are exactly the same as the order of tlvs
 * passed in the registration for the command. Unknown tlvs are ignored.
 * If registered tlvs are missing datap will be NULL. common iov rocessing
 * acquires an input digest to process input buffer. The handler is responsible
 * for constructing an output digest and use packing functions to generate
 * the output buffer. The handler may use the input digest as output digest once
 * the tlv data is extracted and used. Multiple tlv support involves allocation of
 * tlp nodes, except the first, as required,
 */

/* tlp data type indicates if the data is not used/invalid, input or output */
enum {
	BCM_IOV_TLP_NODE_INVALID = 0,
	BCM_IOV_TLP_NODE_IN = 1,
	BCM_IOV_TLP_NODE_OUT = 2
};

struct bcm_iov_tlp {
	uint16 type;
	uint16 len;
	uint16 nodeix;	/* node index */
};

/* tlp data for a given tlv - multiple tlvs of same type chained */
struct bcm_iov_tlp_node {
	uint8 *next;	/* multiple tlv support */
	bcm_iov_tlp_data_type_t type;
	uint8 *data;	/* pointer to data in buffer or state */
};

struct bcm_iov_cmd_digest {
	uint32 version;		/* Version */
	void *cmd_ctx;
	struct wlc_bsscfg *bsscfg;
	const bcm_iov_cmd_info_t *cmd_info;
	uint16 max_tlps;	/* number of tlps allocated */
	uint16 max_nodes;	/* number of nods allocated */
	uint16 num_tlps;	/* number of tlps valid */
	uint16 num_nodes;	/* number of nods valid */
	uint16 tlps_off;	/* offset to tlps */
	uint16 nodes_off;	/* offset to nodes */
	/*
	 * bcm_iov_tlp_t tlps[max_tlps];
	 * bcm_iov_tlp_node_t nodes[max_nodes]
	*/
};

/* get length callback - default length is min_len taken from digest */
typedef size_t (*bcm_iov_xtlv_get_len_t)(const bcm_iov_cmd_digest_t *dig,
	const bcm_iov_cmd_tlv_info_t *tlv_info);

/* pack to buffer data callback. under some conditions it might
 * not be a straight copy and can refer to context(ual) information and
 * endian conversions...
 */
typedef void (*bcm_iov_xtlv_pack_t)(const bcm_iov_cmd_digest_t *dig,
	const bcm_iov_cmd_tlv_info_t *tlv_info,
	uint8 *out_buf, const uint8 *in_data, size_t len);

struct bcm_iov_cmd_tlv_info {
	uint16 id;
	uint16 min_len; /* inclusive */
	uint16 max_len; /* inclusive */
	bcm_iov_xtlv_get_len_t get_len;
	bcm_iov_xtlv_pack_t pack;
};

/*
 * module private parse context. Default version type len is uint16
 */

/* Command parsing options with respect to validation */
/* Possible values for parse context options */
/* Bit 0 - Validate only */
#define BCM_IOV_PARSE_OPT_BATCH_VALIDATE 0x00000001

typedef uint32 bcm_iov_parse_opts_t;

/* get digest callback */
typedef int (*bcm_iov_get_digest_t)(void *cmd_ctx, bcm_iov_cmd_digest_t **dig);

typedef struct bcm_iov_parse_config {
	bcm_iov_parse_opts_t options; /* to handle different ver lengths */
	bcm_iov_malloc_t alloc_fn;
	bcm_iov_free_t free_fn;
	bcm_iov_get_digest_t dig_fn;
	int max_regs;
	void *alloc_ctx;
} bcm_iov_parse_config_t;

/* API */

/* All calls return an integer status code BCME_* unless otherwise indicated */

/* return length of allocation for 'num_cmds' commands. data_len
 * includes length of data for all the commands excluding the headers
 */
size_t bcm_iov_get_alloc_len(int num_cmds, size_t data_len);

/* create parsing context using allocator provided; max_regs provides
 * the number of allowed registrations for commands using the context
 * sub-components of a module may register their own commands indepdently
 * using the parsing context. If digest callback is NULL or returns NULL,
 * the (input) digest is allocated using the provided allocators and released on
 * completion of processing.
 */
int bcm_iov_create_parse_context(const bcm_iov_parse_config_t *parse_cfg,
	bcm_iov_parse_context_t **parse_ctx);

/* free the parsing context; ctx is set to NULL on exit */
int bcm_iov_free_parse_context(bcm_iov_parse_context_t **ctx, bcm_iov_free_t free_fn);

/* Return the command context for the module */
void *bcm_iov_get_cmd_ctx_info(bcm_iov_parse_context_t *parse_ctx);

/* register a command info vector along with supported tlvs. Each command
 * may support a subset of tlvs
 */
int bcm_iov_register_commands(bcm_iov_parse_context_t *parse_ctx, void *cmd_ctx,
	const bcm_iov_cmd_info_t *info, size_t num_cmds,
	const bcm_iov_cmd_tlv_info_t *tlv_info, size_t num_tlvs);

/* pack the xtlvs provided in the digest. may returns BCME_BUFTOOSHORT, but the
 * out_len is set to required length in that case.
 */
int bcm_iov_pack_xtlvs(const bcm_iov_cmd_digest_t *dig,  bcm_xtlv_opts_t xtlv_opts,
	uint8 *out_buf, size_t out_size, size_t *out_len);

#ifdef BCMDRIVER
/* wlc modules register their iovar(s) using the parsing context w/ wlc layer
 * during attach.
 */
struct wlc_if;
struct wlc_info;
extern struct wlc_bsscfg *bcm_iov_bsscfg_find_from_wlcif(struct wlc_info *wlc,
	struct wlc_if *wlcif);
int bcm_iov_doiovar(void *parse_ctx, uint32 id, void *params, uint params_len,
    void *arg, uint arg_len, uint vsize, struct wlc_if *intf);
#endif /* BCMDRIVER */

/* parsing context helpers */

/* get the maximum number of tlvs - can be used to allocate digest for all
 * commands. the digest can be shared. Negative values are BCM_*, >=0, the
 * number of tlvs
 */
int  bcm_iov_parse_get_max_tlvs(const bcm_iov_parse_context_t *ctx);

/* common packing support */

/* pack a buffer of uint8s - memcpy wrapper */
int bcm_iov_pack_buf(const bcm_iov_cmd_digest_t *dig, uint8 *buf,
	const uint8 *data, size_t len);

#define bcm_iov_packv_u8 bcm_iov_pack_buf

/*
 * pack a buffer with uint16s - serialized in LE order, data points to uint16
 * length is not checked.
 */
int bcm_iov_packv_u16(const bcm_iov_cmd_digest_t *dig, uint8 *buf,
	const uint16 *data, int n);

/*
 * pack a buffer with uint32s - serialized in LE order - data points to uint32
 * length is not checked.
 */
int bcm_iov_packv_u32(const bcm_iov_cmd_digest_t *dig, uint8 *buf,
	const uint32 *data, int n);

#endif /* _bcmiov_h_ */
