/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2008-2013 Solarflare Communications Inc.
 */

#ifndef EFX_MCDI_H
#define EFX_MCDI_H

/**
 * enum efx_mcdi_state - MCDI request handling state
 * @MCDI_STATE_QUIESCENT: No pending MCDI requests. If the caller holds the
 *	mcdi @iface_lock then they are able to move to %MCDI_STATE_RUNNING
 * @MCDI_STATE_RUNNING_SYNC: There is a synchronous MCDI request pending.
 *	Only the thread that moved into this state is allowed to move out of it.
 * @MCDI_STATE_RUNNING_ASYNC: There is an asynchronous MCDI request pending.
 * @MCDI_STATE_PROXY_WAIT: An MCDI request has completed with a response that
 *	indicates we must wait for a proxy try again message.
 * @MCDI_STATE_COMPLETED: An MCDI request has completed, but the owning thread
 *	has not yet consumed the result. For all other threads, equivalent to
 *	%MCDI_STATE_RUNNING.
 */
enum efx_mcdi_state {
	MCDI_STATE_QUIESCENT,
	MCDI_STATE_RUNNING_SYNC,
	MCDI_STATE_RUNNING_ASYNC,
	MCDI_STATE_PROXY_WAIT,
	MCDI_STATE_COMPLETED,
};

/**
 * enum efx_mcdi_mode - MCDI transaction mode
 * @MCDI_MODE_POLL: poll for MCDI completion, until timeout
 * @MCDI_MODE_EVENTS: wait for an mcdi_event.  On timeout, poll once
 * @MCDI_MODE_FAIL: we think MCDI is dead, so fail-fast all calls
 */
enum efx_mcdi_mode {
	MCDI_MODE_POLL,
	MCDI_MODE_EVENTS,
	MCDI_MODE_FAIL,
};

/**
 * struct efx_mcdi_iface - MCDI protocol context
 * @efx: The associated NIC.
 * @state: Request handling state. Waited for by @wq.
 * @mode: Poll for mcdi completion, or wait for an mcdi_event.
 * @wq: Wait queue for threads waiting for @state != %MCDI_STATE_RUNNING
 * @new_epoch: Indicates start of day or start of MC reboot recovery
 * @iface_lock: Serialises access to @seqno, @credits and response metadata
 * @seqno: The next sequence number to use for mcdi requests.
 * @credits: Number of spurious MCDI completion events allowed before we
 *     trigger a fatal error
 * @resprc: Response error/success code (Linux numbering)
 * @resp_hdr_len: Response header length
 * @resp_data_len: Response data (SDU or error) length
 * @async_lock: Serialises access to @async_list while event processing is
 *	enabled
 * @async_list: Queue of asynchronous requests
 * @async_timer: Timer for asynchronous request timeout
 * @logging_buffer: buffer that may be used to build MCDI tracing messages
 * @logging_enabled: whether to trace MCDI
 * @proxy_rx_handle: Most recently received proxy authorisation handle
 * @proxy_rx_status: Status of most recent proxy authorisation
 * @proxy_rx_wq: Wait queue for updates to proxy_rx_handle
 */
struct efx_mcdi_iface {
	struct efx_nic *efx;
	enum efx_mcdi_state state;
	enum efx_mcdi_mode mode;
	wait_queue_head_t wq;
	spinlock_t iface_lock;
	bool new_epoch;
	unsigned int credits;
	unsigned int seqno;
	int resprc;
	int resprc_raw;
	size_t resp_hdr_len;
	size_t resp_data_len;
	spinlock_t async_lock;
	struct list_head async_list;
	struct timer_list async_timer;
#ifdef CONFIG_SFC_MCDI_LOGGING
	char *logging_buffer;
	bool logging_enabled;
#endif
	unsigned int proxy_rx_handle;
	int proxy_rx_status;
	wait_queue_head_t proxy_rx_wq;
};

struct efx_mcdi_mon {
	struct efx_buffer dma_buf;
	struct mutex update_lock;
	unsigned long last_update;
	struct device *device;
	struct efx_mcdi_mon_attribute *attrs;
	struct attribute_group group;
	const struct attribute_group *groups[2];
	unsigned int n_attrs;
};

struct efx_mcdi_mtd_partition {
	struct efx_mtd_partition common;
	bool updating;
	u16 nvram_type;
	u16 fw_subtype;
};

#define to_efx_mcdi_mtd_partition(mtd)				\
	container_of(mtd, struct efx_mcdi_mtd_partition, common.mtd)

/**
 * struct efx_mcdi_data - extra state for NICs that implement MCDI
 * @iface: Interface/protocol state
 * @hwmon: Hardware monitor state
 * @fn_flags: Flags for this function, as returned by %MC_CMD_DRV_ATTACH.
 */
struct efx_mcdi_data {
	struct efx_mcdi_iface iface;
#ifdef CONFIG_SFC_MCDI_MON
	struct efx_mcdi_mon hwmon;
#endif
	u32 fn_flags;
};

static inline struct efx_mcdi_iface *efx_mcdi(struct efx_nic *efx)
{
	EFX_WARN_ON_PARANOID(!efx->mcdi);
	return &efx->mcdi->iface;
}

#ifdef CONFIG_SFC_MCDI_MON
static inline struct efx_mcdi_mon *efx_mcdi_mon(struct efx_nic *efx)
{
	EFX_WARN_ON_PARANOID(!efx->mcdi);
	return &efx->mcdi->hwmon;
}
#endif

int efx_mcdi_init(struct efx_nic *efx);
void efx_mcdi_detach(struct efx_nic *efx);
void efx_mcdi_fini(struct efx_nic *efx);

int efx_mcdi_rpc(struct efx_nic *efx, unsigned cmd, const efx_dword_t *inbuf,
		 size_t inlen, efx_dword_t *outbuf, size_t outlen,
		 size_t *outlen_actual);
int efx_mcdi_rpc_quiet(struct efx_nic *efx, unsigned cmd,
		       const efx_dword_t *inbuf, size_t inlen,
		       efx_dword_t *outbuf, size_t outlen,
		       size_t *outlen_actual);

int efx_mcdi_rpc_start(struct efx_nic *efx, unsigned cmd,
		       const efx_dword_t *inbuf, size_t inlen);
int efx_mcdi_rpc_finish(struct efx_nic *efx, unsigned cmd, size_t inlen,
			efx_dword_t *outbuf, size_t outlen,
			size_t *outlen_actual);
int efx_mcdi_rpc_finish_quiet(struct efx_nic *efx, unsigned cmd,
			      size_t inlen, efx_dword_t *outbuf,
			      size_t outlen, size_t *outlen_actual);

typedef void efx_mcdi_async_completer(struct efx_nic *efx,
				      unsigned long cookie, int rc,
				      efx_dword_t *outbuf,
				      size_t outlen_actual);
int efx_mcdi_rpc_async(struct efx_nic *efx, unsigned int cmd,
		       const efx_dword_t *inbuf, size_t inlen, size_t outlen,
		       efx_mcdi_async_completer *complete,
		       unsigned long cookie);
int efx_mcdi_rpc_async_quiet(struct efx_nic *efx, unsigned int cmd,
			     const efx_dword_t *inbuf, size_t inlen,
			     size_t outlen,
			     efx_mcdi_async_completer *complete,
			     unsigned long cookie);

void efx_mcdi_display_error(struct efx_nic *efx, unsigned cmd,
			    size_t inlen, efx_dword_t *outbuf,
			    size_t outlen, int rc);

int efx_mcdi_poll_reboot(struct efx_nic *efx);
void efx_mcdi_mode_poll(struct efx_nic *efx);
void efx_mcdi_mode_event(struct efx_nic *efx);
void efx_mcdi_flush_async(struct efx_nic *efx);

void efx_mcdi_process_event(struct efx_channel *channel, efx_qword_t *event);
void efx_mcdi_sensor_event(struct efx_nic *efx, efx_qword_t *ev);

/* We expect that 16- and 32-bit fields in MCDI requests and responses
 * are appropriately aligned, but 64-bit fields are only
 * 32-bit-aligned.  Also, on Siena we must copy to the MC shared
 * memory strictly 32 bits at a time, so add any necessary padding.
 */
#define MCDI_TX_BUF_LEN(_len) DIV_ROUND_UP((_len), 4)
#define _MCDI_DECLARE_BUF(_name, _len)					\
	efx_dword_t _name[DIV_ROUND_UP(_len, 4)]
#define MCDI_DECLARE_BUF(_name, _len)					\
	_MCDI_DECLARE_BUF(_name, _len) = {{{0}}}
#define MCDI_DECLARE_BUF_ERR(_name)					\
	MCDI_DECLARE_BUF(_name, 8)
#define _MCDI_PTR(_buf, _offset)					\
	((u8 *)(_buf) + (_offset))
#define MCDI_PTR(_buf, _field)						\
	_MCDI_PTR(_buf, MC_CMD_ ## _field ## _OFST)
/* Use MCDI_STRUCT_ functions to access members of MCDI structuredefs.
 * _buf should point to the start of the structure, typically obtained with
 * MCDI_DECLARE_STRUCT_PTR(structure) = _MCDI_DWORD(mcdi_buf, FIELD_WHICH_IS_STRUCT);
 */
#define MCDI_STRUCT_PTR(_buf, _field)					\
	_MCDI_PTR(_buf, _field ## _OFST)
#define _MCDI_CHECK_ALIGN(_ofst, _align)				\
	((_ofst) + BUILD_BUG_ON_ZERO((_ofst) & (_align - 1)))
#define _MCDI_DWORD(_buf, _field)					\
	((_buf) + (_MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _OFST, 4) >> 2))
#define _MCDI_STRUCT_DWORD(_buf, _field)				\
	((_buf) + (_MCDI_CHECK_ALIGN(_field ## _OFST, 4) >> 2))

#define MCDI_STRUCT_SET_BYTE(_buf, _field, _value) do {			\
	BUILD_BUG_ON(_field ## _LEN != 1);				\
	*(u8 *)MCDI_STRUCT_PTR(_buf, _field) = _value;			\
	} while (0)
#define MCDI_BYTE(_buf, _field)						\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 1),	\
	 *MCDI_PTR(_buf, _field))
#define MCDI_SET_WORD(_buf, _field, _value) do {			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _LEN != 2);			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _OFST & 1);			\
	*(__force __le16 *)MCDI_PTR(_buf, _field) = cpu_to_le16(_value);\
	} while (0)
#define MCDI_WORD(_buf, _field)						\
	((u16)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 2) +	\
	 le16_to_cpu(*(__force const __le16 *)MCDI_PTR(_buf, _field)))
#define MCDI_STRUCT_WORD(_buf, _field)                                  \
	((void)BUILD_BUG_ON_ZERO(_field ## _LEN != 2),  \
	le16_to_cpu(*(__force const __le16 *)MCDI_STRUCT_PTR(_buf, _field)))
/* Write a 16-bit field defined in the protocol as being big-endian. */
#define MCDI_SET_WORD_BE(_buf, _field, _value) do {			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _LEN != 2);			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _OFST & 1);			\
	*(__force __be16 *)MCDI_PTR(_buf, _field) = (_value);		\
	} while (0)
#define MCDI_STRUCT_SET_WORD_BE(_buf, _field, _value) do {		\
	BUILD_BUG_ON(_field ## _LEN != 2);				\
	BUILD_BUG_ON(_field ## _OFST & 1);				\
	*(__force __be16 *)MCDI_STRUCT_PTR(_buf, _field) = (_value);	\
	} while (0)
#define MCDI_SET_DWORD(_buf, _field, _value)				\
	EFX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field), EFX_DWORD_0, _value)
#define MCDI_STRUCT_SET_DWORD(_buf, _field, _value)			\
	EFX_POPULATE_DWORD_1(*_MCDI_STRUCT_DWORD(_buf, _field), EFX_DWORD_0, _value)
#define MCDI_DWORD(_buf, _field)					\
	EFX_DWORD_FIELD(*_MCDI_DWORD(_buf, _field), EFX_DWORD_0)
#define MCDI_STRUCT_DWORD(_buf, _field)                                 \
	EFX_DWORD_FIELD(*_MCDI_STRUCT_DWORD(_buf, _field), EFX_DWORD_0)
/* Write a 32-bit field defined in the protocol as being big-endian. */
#define MCDI_STRUCT_SET_DWORD_BE(_buf, _field, _value) do {		\
	BUILD_BUG_ON(_field ## _LEN != 4);				\
	BUILD_BUG_ON(_field ## _OFST & 3);				\
	*(__force __be32 *)MCDI_STRUCT_PTR(_buf, _field) = (_value);	\
	} while (0)
#define MCDI_POPULATE_DWORD_1(_buf, _field, _name1, _value1)		\
	EFX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1)
#define MCDI_POPULATE_DWORD_2(_buf, _field, _name1, _value1,		\
			      _name2, _value2)				\
	EFX_POPULATE_DWORD_2(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2)
#define MCDI_POPULATE_DWORD_3(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3)		\
	EFX_POPULATE_DWORD_3(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3)
#define MCDI_POPULATE_DWORD_4(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4)				\
	EFX_POPULATE_DWORD_4(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4)
#define MCDI_POPULATE_DWORD_5(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5)		\
	EFX_POPULATE_DWORD_5(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5)
#define MCDI_POPULATE_DWORD_6(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5,		\
			      _name6, _value6)				\
	EFX_POPULATE_DWORD_6(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5,		\
			     MC_CMD_ ## _name6, _value6)
#define MCDI_POPULATE_DWORD_7(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5,		\
			      _name6, _value6, _name7, _value7)		\
	EFX_POPULATE_DWORD_7(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5,		\
			     MC_CMD_ ## _name6, _value6,		\
			     MC_CMD_ ## _name7, _value7)
#define MCDI_SET_QWORD(_buf, _field, _value)				\
	do {								\
		EFX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[0],	\
				     EFX_DWORD_0, (u32)(_value));	\
		EFX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[1],	\
				     EFX_DWORD_0, (u64)(_value) >> 32);	\
	} while (0)
#define MCDI_QWORD(_buf, _field)					\
	(EFX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[0], EFX_DWORD_0) |	\
	(u64)EFX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[1], EFX_DWORD_0) << 32)
#define MCDI_FIELD(_ptr, _type, _field)					\
	EFX_EXTRACT_DWORD(						\
		*(efx_dword_t *)					\
		_MCDI_PTR(_ptr, MC_CMD_ ## _type ## _ ## _field ## _OFST & ~3),\
		MC_CMD_ ## _type ## _ ## _field ## _LBN & 0x1f,	\
		(MC_CMD_ ## _type ## _ ## _field ## _LBN & 0x1f) +	\
		MC_CMD_ ## _type ## _ ## _field ## _WIDTH - 1)

#define _MCDI_ARRAY_PTR(_buf, _field, _index, _align)			\
	(_MCDI_PTR(_buf, _MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _OFST, _align))\
	 + (_index) * _MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _LEN, _align))
#define MCDI_DECLARE_STRUCT_PTR(_name)					\
	efx_dword_t *_name
#define MCDI_ARRAY_STRUCT_PTR(_buf, _field, _index)			\
	((efx_dword_t *)_MCDI_ARRAY_PTR(_buf, _field, _index, 4))
#define MCDI_VAR_ARRAY_LEN(_len, _field)				\
	min_t(size_t, MC_CMD_ ## _field ## _MAXNUM,			\
	      ((_len) - MC_CMD_ ## _field ## _OFST) / MC_CMD_ ## _field ## _LEN)
#define MCDI_ARRAY_WORD(_buf, _field, _index)				\
	(BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 2) +		\
	 le16_to_cpu(*(__force const __le16 *)				\
		     _MCDI_ARRAY_PTR(_buf, _field, _index, 2)))
#define _MCDI_ARRAY_DWORD(_buf, _field, _index)				\
	(BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 4) +		\
	 (efx_dword_t *)_MCDI_ARRAY_PTR(_buf, _field, _index, 4))
#define MCDI_SET_ARRAY_DWORD(_buf, _field, _index, _value)		\
	EFX_SET_DWORD_FIELD(*_MCDI_ARRAY_DWORD(_buf, _field, _index),	\
			    EFX_DWORD_0, _value)
#define MCDI_ARRAY_DWORD(_buf, _field, _index)				\
	EFX_DWORD_FIELD(*_MCDI_ARRAY_DWORD(_buf, _field, _index), EFX_DWORD_0)
#define _MCDI_ARRAY_QWORD(_buf, _field, _index)				\
	(BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 8) +		\
	 (efx_dword_t *)_MCDI_ARRAY_PTR(_buf, _field, _index, 4))
#define MCDI_SET_ARRAY_QWORD(_buf, _field, _index, _value)		\
	do {								\
		EFX_SET_DWORD_FIELD(_MCDI_ARRAY_QWORD(_buf, _field, _index)[0],\
				    EFX_DWORD_0, (u32)(_value));	\
		EFX_SET_DWORD_FIELD(_MCDI_ARRAY_QWORD(_buf, _field, _index)[1],\
				    EFX_DWORD_0, (u64)(_value) >> 32);	\
	} while (0)
#define MCDI_ARRAY_FIELD(_buf, _field1, _type, _index, _field2)		\
	MCDI_FIELD(MCDI_ARRAY_STRUCT_PTR(_buf, _field1, _index),	\
		   _type ## _TYPEDEF, _field2)

#define MCDI_EVENT_FIELD(_ev, _field)			\
	EFX_QWORD_FIELD(_ev, MCDI_EVENT_ ## _field)

#define MCDI_CAPABILITY(field)						\
	MC_CMD_GET_CAPABILITIES_V8_OUT_ ## field ## _LBN

#define MCDI_CAPABILITY_OFST(field) \
	MC_CMD_GET_CAPABILITIES_V8_OUT_ ## field ## _OFST

#define efx_has_cap(efx, field) \
	efx->type->check_caps(efx, \
			      MCDI_CAPABILITY(field), \
			      MCDI_CAPABILITY_OFST(field))

void efx_mcdi_print_fwver(struct efx_nic *efx, char *buf, size_t len);
int efx_mcdi_get_board_cfg(struct efx_nic *efx, u8 *mac_address,
			   u16 *fw_subtype_list, u32 *capabilities);
int efx_mcdi_log_ctrl(struct efx_nic *efx, bool evq, bool uart, u32 dest_evq);
int efx_mcdi_nvram_types(struct efx_nic *efx, u32 *nvram_types_out);
int efx_mcdi_nvram_info(struct efx_nic *efx, unsigned int type,
			size_t *size_out, size_t *erase_size_out,
			bool *protected_out);
int efx_new_mcdi_nvram_test_all(struct efx_nic *efx);
int efx_mcdi_nvram_metadata(struct efx_nic *efx, unsigned int type,
			    u32 *subtype, u16 version[4], char *desc,
			    size_t descsize);
int efx_mcdi_nvram_test_all(struct efx_nic *efx);
int efx_mcdi_handle_assertion(struct efx_nic *efx);
int efx_mcdi_set_id_led(struct efx_nic *efx, enum efx_led_mode mode);
int efx_mcdi_wol_filter_set_magic(struct efx_nic *efx, const u8 *mac,
				  int *id_out);
int efx_mcdi_wol_filter_get_magic(struct efx_nic *efx, int *id_out);
int efx_mcdi_wol_filter_remove(struct efx_nic *efx, int id);
int efx_mcdi_wol_filter_reset(struct efx_nic *efx);
int efx_mcdi_flush_rxqs(struct efx_nic *efx);
void efx_mcdi_process_link_change(struct efx_nic *efx, efx_qword_t *ev);
void efx_mcdi_mac_start_stats(struct efx_nic *efx);
void efx_mcdi_mac_stop_stats(struct efx_nic *efx);
void efx_mcdi_mac_pull_stats(struct efx_nic *efx);
enum reset_type efx_mcdi_map_reset_reason(enum reset_type reason);
int efx_mcdi_reset(struct efx_nic *efx, enum reset_type method);
int efx_mcdi_set_workaround(struct efx_nic *efx, u32 type, bool enabled,
			    unsigned int *flags);
int efx_mcdi_get_workarounds(struct efx_nic *efx, unsigned int *impl_out,
			     unsigned int *enabled_out);
int efx_mcdi_get_privilege_mask(struct efx_nic *efx, u32 *mask);

#ifdef CONFIG_SFC_MCDI_MON
int efx_mcdi_mon_probe(struct efx_nic *efx);
void efx_mcdi_mon_remove(struct efx_nic *efx);
#else
static inline int efx_mcdi_mon_probe(struct efx_nic *efx) { return 0; }
static inline void efx_mcdi_mon_remove(struct efx_nic *efx) {}
#endif

#ifdef CONFIG_SFC_MTD
int efx_mcdi_mtd_read(struct mtd_info *mtd, loff_t start, size_t len,
		      size_t *retlen, u8 *buffer);
int efx_mcdi_mtd_erase(struct mtd_info *mtd, loff_t start, size_t len);
int efx_mcdi_mtd_write(struct mtd_info *mtd, loff_t start, size_t len,
		       size_t *retlen, const u8 *buffer);
int efx_mcdi_mtd_sync(struct mtd_info *mtd);
void efx_mcdi_mtd_rename(struct efx_mtd_partition *part);
#endif

#endif /* EFX_MCDI_H */
