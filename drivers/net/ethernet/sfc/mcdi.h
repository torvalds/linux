/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2008-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_MCDI_H
#define EFX_MCDI_H

/**
 * enum efx_mcdi_state
 * @MCDI_STATE_QUIESCENT: No pending MCDI requests. If the caller holds the
 *	mcdi_lock then they are able to move to MCDI_STATE_RUNNING
 * @MCDI_STATE_RUNNING: There is an MCDI request pending. Only the thread that
 *	moved into this state is allowed to move out of it.
 * @MCDI_STATE_COMPLETED: An MCDI request has completed, but the owning thread
 *	has not yet consumed the result. For all other threads, equivalent to
 *	MCDI_STATE_RUNNING.
 */
enum efx_mcdi_state {
	MCDI_STATE_QUIESCENT,
	MCDI_STATE_RUNNING,
	MCDI_STATE_COMPLETED,
};

enum efx_mcdi_mode {
	MCDI_MODE_POLL,
	MCDI_MODE_EVENTS,
};

/**
 * struct efx_mcdi_iface
 * @state: Interface state. Waited for by mcdi_wq.
 * @wq: Wait queue for threads waiting for state != STATE_RUNNING
 * @iface_lock: Protects @credits, @seqno, @resprc, @resplen
 * @mode: Poll for mcdi completion, or wait for an mcdi_event.
 *	Serialised by @lock
 * @seqno: The next sequence number to use for mcdi requests.
 *	Serialised by @lock
 * @credits: Number of spurious MCDI completion events allowed before we
 *	trigger a fatal error. Protected by @lock
 * @resprc: Returned MCDI completion
 * @resplen: Returned payload length
 */
struct efx_mcdi_iface {
	atomic_t state;
	wait_queue_head_t wq;
	spinlock_t iface_lock;
	enum efx_mcdi_mode mode;
	unsigned int credits;
	unsigned int seqno;
	unsigned int resprc;
	size_t resplen;
};

struct efx_mcdi_mon {
	struct efx_buffer dma_buf;
	struct mutex update_lock;
	unsigned long last_update;
	struct device *device;
	struct efx_mcdi_mon_attribute *attrs;
	unsigned int n_attrs;
};

extern void efx_mcdi_init(struct efx_nic *efx);

extern int efx_mcdi_rpc(struct efx_nic *efx, unsigned cmd,
			const efx_dword_t *inbuf, size_t inlen,
			efx_dword_t *outbuf, size_t outlen,
			size_t *outlen_actual);

extern void efx_mcdi_rpc_start(struct efx_nic *efx, unsigned cmd,
			       const efx_dword_t *inbuf, size_t inlen);
extern int efx_mcdi_rpc_finish(struct efx_nic *efx, unsigned cmd, size_t inlen,
			       efx_dword_t *outbuf, size_t outlen,
			       size_t *outlen_actual);


extern int efx_mcdi_poll_reboot(struct efx_nic *efx);
extern void efx_mcdi_mode_poll(struct efx_nic *efx);
extern void efx_mcdi_mode_event(struct efx_nic *efx);

extern void efx_mcdi_process_event(struct efx_channel *channel,
				   efx_qword_t *event);
extern void efx_mcdi_sensor_event(struct efx_nic *efx, efx_qword_t *ev);

/* We expect that 16- and 32-bit fields in MCDI requests and responses
 * are appropriately aligned, but 64-bit fields are only
 * 32-bit-aligned.  Also, on Siena we must copy to the MC shared
 * memory strictly 32 bits at a time, so add any necessary padding.
 */
#define MCDI_DECLARE_BUF(_name, _len)					\
	efx_dword_t _name[DIV_ROUND_UP(_len, 4)]
#define _MCDI_PTR(_buf, _offset)					\
	((u8 *)(_buf) + (_offset))
#define MCDI_PTR(_buf, _field)						\
	_MCDI_PTR(_buf, MC_CMD_ ## _field ## _OFST)
#define _MCDI_CHECK_ALIGN(_ofst, _align)				\
	((_ofst) + BUILD_BUG_ON_ZERO((_ofst) & (_align - 1)))
#define _MCDI_DWORD(_buf, _field)					\
	((_buf) + (_MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _OFST, 4) >> 2))

#define MCDI_SET_DWORD(_buf, _field, _value)				\
	EFX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field), EFX_DWORD_0, _value)
#define MCDI_DWORD(_buf, _field)					\
	EFX_DWORD_FIELD(*_MCDI_DWORD(_buf, _field), EFX_DWORD_0)
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

extern void efx_mcdi_print_fwver(struct efx_nic *efx, char *buf, size_t len);
extern int efx_mcdi_drv_attach(struct efx_nic *efx, bool driver_operating,
			       bool *was_attached_out);
extern int efx_mcdi_get_board_cfg(struct efx_nic *efx, u8 *mac_address,
				  u16 *fw_subtype_list, u32 *capabilities);
extern int efx_mcdi_log_ctrl(struct efx_nic *efx, bool evq, bool uart,
			     u32 dest_evq);
extern int efx_mcdi_nvram_types(struct efx_nic *efx, u32 *nvram_types_out);
extern int efx_mcdi_nvram_info(struct efx_nic *efx, unsigned int type,
			       size_t *size_out, size_t *erase_size_out,
			       bool *protected_out);
extern int efx_mcdi_nvram_update_start(struct efx_nic *efx,
				       unsigned int type);
extern int efx_mcdi_nvram_read(struct efx_nic *efx, unsigned int type,
			       loff_t offset, u8 *buffer, size_t length);
extern int efx_mcdi_nvram_write(struct efx_nic *efx, unsigned int type,
				loff_t offset, const u8 *buffer,
				size_t length);
#define EFX_MCDI_NVRAM_LEN_MAX 128
extern int efx_mcdi_nvram_erase(struct efx_nic *efx, unsigned int type,
				loff_t offset, size_t length);
extern int efx_mcdi_nvram_update_finish(struct efx_nic *efx,
					unsigned int type);
extern int efx_mcdi_nvram_test_all(struct efx_nic *efx);
extern int efx_mcdi_handle_assertion(struct efx_nic *efx);
extern void efx_mcdi_set_id_led(struct efx_nic *efx, enum efx_led_mode mode);
extern int efx_mcdi_wol_filter_set_magic(struct efx_nic *efx,
					 const u8 *mac, int *id_out);
extern int efx_mcdi_wol_filter_get_magic(struct efx_nic *efx, int *id_out);
extern int efx_mcdi_wol_filter_remove(struct efx_nic *efx, int id);
extern int efx_mcdi_wol_filter_reset(struct efx_nic *efx);
extern int efx_mcdi_flush_rxqs(struct efx_nic *efx);
extern int efx_mcdi_port_probe(struct efx_nic *efx);
extern void efx_mcdi_port_remove(struct efx_nic *efx);
extern int efx_mcdi_port_reconfigure(struct efx_nic *efx);
extern void efx_mcdi_process_link_change(struct efx_nic *efx, efx_qword_t *ev);
extern int efx_mcdi_set_mac(struct efx_nic *efx);
#define EFX_MC_STATS_GENERATION_INVALID ((__force __le64)(-1))
extern void efx_mcdi_mac_start_stats(struct efx_nic *efx);
extern void efx_mcdi_mac_stop_stats(struct efx_nic *efx);
extern bool efx_mcdi_mac_check_fault(struct efx_nic *efx);
extern enum reset_type efx_mcdi_map_reset_reason(enum reset_type reason);
extern int efx_mcdi_reset(struct efx_nic *efx, enum reset_type method);

#ifdef CONFIG_SFC_MCDI_MON
extern int efx_mcdi_mon_probe(struct efx_nic *efx);
extern void efx_mcdi_mon_remove(struct efx_nic *efx);
#else
static inline int efx_mcdi_mon_probe(struct efx_nic *efx) { return 0; }
static inline void efx_mcdi_mon_remove(struct efx_nic *efx) {}
#endif

#endif /* EFX_MCDI_H */
