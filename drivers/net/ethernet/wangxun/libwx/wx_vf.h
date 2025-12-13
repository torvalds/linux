/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_VF_H_
#define _WX_VF_H_

/* Control registers */
#define WX_VF_MAX_RING_NUMS      8
#define WX_VX_PF_BME             0x4B8
#define WX_VF_BME_ENABLE         BIT(0)
#define WX_VXSTATUS              0x4
#define WX_VXCTRL                0x8
#define WX_VXCTRL_RST            BIT(0)

#define WX_VXMRQC                0x78
#define WX_VXMRQC_PSR_L4HDR      BIT(0)
#define WX_VXMRQC_PSR_L3HDR      BIT(1)
#define WX_VXMRQC_PSR_L2HDR      BIT(2)
#define WX_VXMRQC_PSR_TUNHDR     BIT(3)
#define WX_VXMRQC_PSR_TUNMAC     BIT(4)
#define WX_VXMRQC_PSR_MASK       GENMASK(5, 1)
#define WX_VXMRQC_PSR(f)         FIELD_PREP(GENMASK(5, 1), f)
#define WX_VXMRQC_RSS_HASH(f)    FIELD_PREP(GENMASK(15, 13), f)
#define WX_VXMRQC_RSS_MASK       GENMASK(31, 16)
#define WX_VXMRQC_RSS(f)         FIELD_PREP(GENMASK(31, 16), f)
#define WX_VXMRQC_RSS_ALG_IPV4_TCP   BIT(0)
#define WX_VXMRQC_RSS_ALG_IPV4       BIT(1)
#define WX_VXMRQC_RSS_ALG_IPV6       BIT(4)
#define WX_VXMRQC_RSS_ALG_IPV6_TCP   BIT(5)
#define WX_VXMRQC_RSS_EN             BIT(8)

#define WX_VXRSSRK(i)            (0x80 + ((i) * 4)) /* i=[0,9] */
#define WX_VXRETA(i)             (0xC0 + ((i) * 4)) /* i=[0,15] */

/* Interrupt registers */
#define WX_VXICR                 0x100
#define WX_VXIMS                 0x108
#define WX_VXIMC                 0x10C
#define WX_VF_IRQ_CLEAR_MASK     7
#define WX_VF_MAX_TX_QUEUES      4
#define WX_VF_MAX_RX_QUEUES      4

#define WX_VXITR(i)              (0x200 + (4 * (i))) /* i=[0,1] */
#define WX_VXITR_MASK            GENMASK(8, 0)
#define WX_VXITR_CNT_WDIS        BIT(31)
#define WX_VXIVAR_MISC           0x260
#define WX_VXIVAR(i)             (0x240 + (4 * (i))) /* i=[0,3] */

#define wx_conf_size(v, mwidth, uwidth) ({ \
	typeof(v) _v = (v); \
	(_v == 2 << (mwidth) ? 0 : _v >> (uwidth)); \
})
#define wx_buf_len(v)            wx_conf_size(v, 13, 7)
#define wx_hdr_sz(v)             wx_conf_size(v, 10, 6)
#define wx_buf_sz(v)             wx_conf_size(v, 14, 10)
#define wx_pkt_thresh(v)         wx_conf_size(v, 4, 0)

#define WX_RX_HDR_SIZE           256
#define WX_RX_BUF_SIZE           2048

#define WX_RXBUFFER_2048         (2048)
#define WX_RXBUFFER_3072         3072

/* Receive Path */
#define WX_VXRDBAL(r)            (0x1000 + (0x40 * (r)))
#define WX_VXRDBAH(r)            (0x1004 + (0x40 * (r)))
#define WX_VXRDT(r)              (0x1008 + (0x40 * (r)))
#define WX_VXRDH(r)              (0x100C + (0x40 * (r)))
#define WX_VXRXDCTL(r)           (0x1010 + (0x40 * (r)))
#define WX_VXRXDCTL_ENABLE       BIT(0)
#define WX_VXRXDCTL_BUFLEN_MASK  GENMASK(6, 1)
#define WX_VXRXDCTL_BUFLEN(f)    FIELD_PREP(GENMASK(6, 1), f)
#define WX_VXRXDCTL_BUFSZ_MASK   GENMASK(11, 8)
#define WX_VXRXDCTL_BUFSZ(f)     FIELD_PREP(GENMASK(11, 8), f)
#define WX_VXRXDCTL_HDRSZ_MASK   GENMASK(15, 12)
#define WX_VXRXDCTL_HDRSZ(f)     FIELD_PREP(GENMASK(15, 12), f)
#define WX_VXRXDCTL_RSCMAX_MASK  GENMASK(24, 23)
#define WX_VXRXDCTL_RSCMAX(f)    FIELD_PREP(GENMASK(24, 23), f)
#define WX_VXRXDCTL_RSCEN        BIT(29)
#define WX_VXRXDCTL_DROP         BIT(30)
#define WX_VXRXDCTL_VLAN         BIT(31)

/* Transimit Path */
#define WX_VXTDBAL(r)            (0x3000 + (0x40 * (r)))
#define WX_VXTDBAH(r)            (0x3004 + (0x40 * (r)))
#define WX_VXTDT(r)              (0x3008 + (0x40 * (r)))
#define WX_VXTDH(r)              (0x300C + (0x40 * (r)))
#define WX_VXTXDCTL(r)           (0x3010 + (0x40 * (r)))
#define WX_VXTXDCTL_ENABLE       BIT(0)
#define WX_VXTXDCTL_BUFLEN(f)    FIELD_PREP(GENMASK(6, 1), f)
#define WX_VXTXDCTL_PTHRESH(f)   FIELD_PREP(GENMASK(11, 8), f)
#define WX_VXTXDCTL_WTHRESH(f)   FIELD_PREP(GENMASK(22, 16), f)
#define WX_VXTXDCTL_FLUSH        BIT(26)

#define WX_PFLINK_STATUS(g)      FIELD_GET(BIT(0), g)
#define WX_PFLINK_SPEED(g)       FIELD_GET(GENMASK(31, 1), g)
#define WX_VXSTATUS_SPEED(g)     FIELD_GET(GENMASK(4, 1), g)

struct wx_link_reg_fields {
	u32 mac_type;
	u32 bit0_f;
	u32 bit1_f;
	u32 bit2_f;
	u32 bit3_f;
	u32 bit4_f;
};

void wx_init_hw_vf(struct wx *wx);
int wx_reset_hw_vf(struct wx *wx);
void wx_get_mac_addr_vf(struct wx *wx, u8 *mac_addr);
void wx_stop_adapter_vf(struct wx *wx);
int wx_get_fw_version_vf(struct wx *wx);
int wx_set_rar_vf(struct wx *wx, u32 index, u8 *addr, u32 enable_addr);
int wx_update_mc_addr_list_vf(struct wx *wx, struct net_device *netdev);
int wx_set_uc_addr_vf(struct wx *wx, u32 index, u8 *addr);
int wx_rlpml_set_vf(struct wx *wx, u16 max_size);
int wx_negotiate_api_version(struct wx *wx, int api);
int wx_get_queues_vf(struct wx *wx, u32 *num_tcs, u32 *default_tc);
int wx_update_xcast_mode_vf(struct wx *wx, int xcast_mode);
int wx_get_link_state_vf(struct wx *wx, u16 *link_state);
int wx_set_vfta_vf(struct wx *wx, u32 vlan, u32 vind, bool vlan_on,
		   bool vlvf_bypass);
int wx_check_mac_link_vf(struct wx *wx);

#endif /* _WX_VF_H_ */
