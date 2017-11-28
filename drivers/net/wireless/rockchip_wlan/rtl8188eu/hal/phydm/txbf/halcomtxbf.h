#ifndef __HAL_COM_TXBF_H__
#define __HAL_COM_TXBF_H__

/*
typedef	bool
(*TXBF_GET)(
	void*			p_adapter,
	u8			get_type,
	void*			p_out_buf
	);

typedef	bool
(*TXBF_SET)(
	void*			p_adapter,
	u8			set_type,
	void*			p_in_buf
	);
*/

enum txbf_set_type {
	TXBF_SET_SOUNDING_ENTER,
	TXBF_SET_SOUNDING_LEAVE,
	TXBF_SET_SOUNDING_RATE,
	TXBF_SET_SOUNDING_STATUS,
	TXBF_SET_SOUNDING_FW_NDPA,
	TXBF_SET_SOUNDING_CLK,
	TXBF_SET_TX_PATH_RESET,
	TXBF_SET_GET_TX_RATE
};


enum txbf_get_type {
	TXBF_GET_EXPLICIT_BEAMFORMEE,
	TXBF_GET_EXPLICIT_BEAMFORMER,
	TXBF_GET_MU_MIMO_STA,
	TXBF_GET_MU_MIMO_AP
};



/* 2 HAL TXBF related */
struct _HAL_TXBF_INFO {
	u8				txbf_idx;
	u8				ndpa_idx;
	u8				BW;
	u8				rate;

	struct timer_list			txbf_fw_ndpa_timer;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_WORK_ITEM		txbf_enter_work_item;
	RT_WORK_ITEM		txbf_leave_work_item;
	RT_WORK_ITEM		txbf_fw_ndpa_work_item;
	RT_WORK_ITEM		txbf_clk_work_item;
	RT_WORK_ITEM		txbf_status_work_item;
	RT_WORK_ITEM		txbf_rate_work_item;
	RT_WORK_ITEM		txbf_reset_tx_path_work_item;
	RT_WORK_ITEM		txbf_get_tx_rate_work_item;
#endif

};

#if (BEAMFORMING_SUPPORT == 1)

void
hal_com_txbf_beamform_init(
	void			*p_dm_void
);

void
hal_com_txbf_config_gtab(
	void			*p_dm_void
);

void
hal_com_txbf_enter_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

void
hal_com_txbf_leave_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

void
hal_com_txbf_fw_ndpa_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

void
hal_com_txbf_clk_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

void
hal_com_txbf_reset_tx_path_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

void
hal_com_txbf_get_tx_rate_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

void
hal_com_txbf_rate_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

void
hal_com_txbf_fw_ndpa_timer_callback(
	struct timer_list		*p_timer
);

void
hal_com_txbf_status_work_item_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter
#else
	void			*p_dm_void
#endif
);

bool
hal_com_txbf_set(
	void			*p_dm_void,
	u8			set_type,
	void			*p_in_buf
);

bool
hal_com_txbf_get(
	struct _ADAPTER		*adapter,
	u8			get_type,
	void			*p_out_buf
);

#else
#define hal_com_txbf_beamform_init(p_dm_void)					NULL
#define hal_com_txbf_config_gtab(p_dm_void)				NULL
#define hal_com_txbf_enter_work_item_callback(_adapter)		NULL
#define hal_com_txbf_leave_work_item_callback(_adapter)		NULL
#define hal_com_txbf_fw_ndpa_work_item_callback(_adapter)		NULL
#define hal_com_txbf_clk_work_item_callback(_adapter)			NULL
#define hal_com_txbf_rate_work_item_callback(_adapter)		NULL
#define hal_com_txbf_fw_ndpa_timer_callback(_adapter)		NULL
#define hal_com_txbf_status_work_item_callback(_adapter)		NULL
#define hal_com_txbf_get(_adapter, _get_type, _pout_buf)

#endif

#endif	/*  #ifndef __HAL_COM_TXBF_H__ */
