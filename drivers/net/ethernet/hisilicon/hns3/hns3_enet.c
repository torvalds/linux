// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#ifdef CONFIG_RFS_ACCEL
#include <linux/cpu_rmap.h>
#endif
#include <linux/if_vlan.h>
#include <linux/irq.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/skbuff.h>
#include <linux/sctp.h>
#include <net/gre.h>
#include <net/ip6_checksum.h>
#include <net/pkt_cls.h>
#include <net/tcp.h>
#include <net/vxlan.h>
#include <net/geneve.h>

#include "hnae3.h"
#include "hns3_enet.h"
/* All hns3 tracepoints are defined by the include below, which
 * must be included exactly once across the whole kernel with
 * CREATE_TRACE_POINTS defined
 */
#define CREATE_TRACE_POINTS
#include "hns3_trace.h"

#define hns3_set_field(origin, shift, val)	((origin) |= (val) << (shift))
#define hns3_tx_bd_count(S)	DIV_ROUND_UP(S, HNS3_MAX_BD_SIZE)

#define hns3_rl_err(fmt, ...)						\
	do {								\
		if (net_ratelimit())					\
			netdev_err(fmt, ##__VA_ARGS__);			\
	} while (0)

static void hns3_clear_all_ring(struct hnae3_handle *h, bool force);

static const char hns3_driver_name[] = "hns3";
static const char hns3_driver_string[] =
			"Hisilicon Ethernet Network Driver for Hip08 Family";
static const char hns3_copyright[] = "Copyright (c) 2017 Huawei Corporation.";
static struct hnae3_client client;

static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, " Network interface message level setting");

static unsigned int tx_spare_buf_size;
module_param(tx_spare_buf_size, uint, 0400);
MODULE_PARM_DESC(tx_spare_buf_size, "Size used to allocate tx spare buffer");

static unsigned int tx_sgl = 1;
module_param(tx_sgl, uint, 0600);
MODULE_PARM_DESC(tx_sgl, "Minimum number of frags when using dma_map_sg() to optimize the IOMMU mapping");

static bool page_pool_enabled = true;
module_param(page_pool_enabled, bool, 0400);

#define HNS3_SGL_SIZE(nfrag)	(sizeof(struct scatterlist) * (nfrag) +	\
				 sizeof(struct sg_table))
#define HNS3_MAX_SGL_SIZE	ALIGN(HNS3_SGL_SIZE(HNS3_MAX_TSO_BD_NUM), \
				      dma_get_cache_alignment())

#define DEFAULT_MSG_LEVEL (NETIF_MSG_PROBE | NETIF_MSG_LINK | \
			   NETIF_MSG_IFDOWN | NETIF_MSG_IFUP)

#define HNS3_INNER_VLAN_TAG	1
#define HNS3_OUTER_VLAN_TAG	2

#define HNS3_MIN_TX_LEN		33U
#define HNS3_MIN_TUN_PKT_LEN	65U

/* hns3_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id hns3_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_200G_RDMA),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_VF), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_RDMA_DCB_PFC_VF),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	/* required last entry */
	{0,}
};
MODULE_DEVICE_TABLE(pci, hns3_pci_tbl);

#define HNS3_RX_PTYPE_ENTRY(ptype, l, s, t) \
	{	ptype, \
		l, \
		CHECKSUM_##s, \
		HNS3_L3_TYPE_##t, \
		1 }

#define HNS3_RX_PTYPE_UNUSED_ENTRY(ptype) \
		{ ptype, 0, CHECKSUM_NONE, HNS3_L3_TYPE_PARSE_FAIL, 0 }

static const struct hns3_rx_ptype hns3_rx_ptype_tbl[] = {
	HNS3_RX_PTYPE_UNUSED_ENTRY(0),
	HNS3_RX_PTYPE_ENTRY(1, 0, COMPLETE, ARP),
	HNS3_RX_PTYPE_ENTRY(2, 0, COMPLETE, RARP),
	HNS3_RX_PTYPE_ENTRY(3, 0, COMPLETE, LLDP),
	HNS3_RX_PTYPE_ENTRY(4, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(5, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(6, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(7, 0, COMPLETE, CNM),
	HNS3_RX_PTYPE_ENTRY(8, 0, NONE, PARSE_FAIL),
	HNS3_RX_PTYPE_UNUSED_ENTRY(9),
	HNS3_RX_PTYPE_UNUSED_ENTRY(10),
	HNS3_RX_PTYPE_UNUSED_ENTRY(11),
	HNS3_RX_PTYPE_UNUSED_ENTRY(12),
	HNS3_RX_PTYPE_UNUSED_ENTRY(13),
	HNS3_RX_PTYPE_UNUSED_ENTRY(14),
	HNS3_RX_PTYPE_UNUSED_ENTRY(15),
	HNS3_RX_PTYPE_ENTRY(16, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(17, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(18, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(19, 0, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(20, 0, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(21, 0, NONE, IPV4),
	HNS3_RX_PTYPE_ENTRY(22, 0, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(23, 0, NONE, IPV4),
	HNS3_RX_PTYPE_ENTRY(24, 0, NONE, IPV4),
	HNS3_RX_PTYPE_ENTRY(25, 0, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_UNUSED_ENTRY(26),
	HNS3_RX_PTYPE_UNUSED_ENTRY(27),
	HNS3_RX_PTYPE_UNUSED_ENTRY(28),
	HNS3_RX_PTYPE_ENTRY(29, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(30, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(31, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(32, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(33, 1, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(34, 1, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(35, 1, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(36, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(37, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_UNUSED_ENTRY(38),
	HNS3_RX_PTYPE_ENTRY(39, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(40, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(41, 1, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(42, 1, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(43, 1, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(44, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(45, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_UNUSED_ENTRY(46),
	HNS3_RX_PTYPE_UNUSED_ENTRY(47),
	HNS3_RX_PTYPE_UNUSED_ENTRY(48),
	HNS3_RX_PTYPE_UNUSED_ENTRY(49),
	HNS3_RX_PTYPE_UNUSED_ENTRY(50),
	HNS3_RX_PTYPE_UNUSED_ENTRY(51),
	HNS3_RX_PTYPE_UNUSED_ENTRY(52),
	HNS3_RX_PTYPE_UNUSED_ENTRY(53),
	HNS3_RX_PTYPE_UNUSED_ENTRY(54),
	HNS3_RX_PTYPE_UNUSED_ENTRY(55),
	HNS3_RX_PTYPE_UNUSED_ENTRY(56),
	HNS3_RX_PTYPE_UNUSED_ENTRY(57),
	HNS3_RX_PTYPE_UNUSED_ENTRY(58),
	HNS3_RX_PTYPE_UNUSED_ENTRY(59),
	HNS3_RX_PTYPE_UNUSED_ENTRY(60),
	HNS3_RX_PTYPE_UNUSED_ENTRY(61),
	HNS3_RX_PTYPE_UNUSED_ENTRY(62),
	HNS3_RX_PTYPE_UNUSED_ENTRY(63),
	HNS3_RX_PTYPE_UNUSED_ENTRY(64),
	HNS3_RX_PTYPE_UNUSED_ENTRY(65),
	HNS3_RX_PTYPE_UNUSED_ENTRY(66),
	HNS3_RX_PTYPE_UNUSED_ENTRY(67),
	HNS3_RX_PTYPE_UNUSED_ENTRY(68),
	HNS3_RX_PTYPE_UNUSED_ENTRY(69),
	HNS3_RX_PTYPE_UNUSED_ENTRY(70),
	HNS3_RX_PTYPE_UNUSED_ENTRY(71),
	HNS3_RX_PTYPE_UNUSED_ENTRY(72),
	HNS3_RX_PTYPE_UNUSED_ENTRY(73),
	HNS3_RX_PTYPE_UNUSED_ENTRY(74),
	HNS3_RX_PTYPE_UNUSED_ENTRY(75),
	HNS3_RX_PTYPE_UNUSED_ENTRY(76),
	HNS3_RX_PTYPE_UNUSED_ENTRY(77),
	HNS3_RX_PTYPE_UNUSED_ENTRY(78),
	HNS3_RX_PTYPE_UNUSED_ENTRY(79),
	HNS3_RX_PTYPE_UNUSED_ENTRY(80),
	HNS3_RX_PTYPE_UNUSED_ENTRY(81),
	HNS3_RX_PTYPE_UNUSED_ENTRY(82),
	HNS3_RX_PTYPE_UNUSED_ENTRY(83),
	HNS3_RX_PTYPE_UNUSED_ENTRY(84),
	HNS3_RX_PTYPE_UNUSED_ENTRY(85),
	HNS3_RX_PTYPE_UNUSED_ENTRY(86),
	HNS3_RX_PTYPE_UNUSED_ENTRY(87),
	HNS3_RX_PTYPE_UNUSED_ENTRY(88),
	HNS3_RX_PTYPE_UNUSED_ENTRY(89),
	HNS3_RX_PTYPE_UNUSED_ENTRY(90),
	HNS3_RX_PTYPE_UNUSED_ENTRY(91),
	HNS3_RX_PTYPE_UNUSED_ENTRY(92),
	HNS3_RX_PTYPE_UNUSED_ENTRY(93),
	HNS3_RX_PTYPE_UNUSED_ENTRY(94),
	HNS3_RX_PTYPE_UNUSED_ENTRY(95),
	HNS3_RX_PTYPE_UNUSED_ENTRY(96),
	HNS3_RX_PTYPE_UNUSED_ENTRY(97),
	HNS3_RX_PTYPE_UNUSED_ENTRY(98),
	HNS3_RX_PTYPE_UNUSED_ENTRY(99),
	HNS3_RX_PTYPE_UNUSED_ENTRY(100),
	HNS3_RX_PTYPE_UNUSED_ENTRY(101),
	HNS3_RX_PTYPE_UNUSED_ENTRY(102),
	HNS3_RX_PTYPE_UNUSED_ENTRY(103),
	HNS3_RX_PTYPE_UNUSED_ENTRY(104),
	HNS3_RX_PTYPE_UNUSED_ENTRY(105),
	HNS3_RX_PTYPE_UNUSED_ENTRY(106),
	HNS3_RX_PTYPE_UNUSED_ENTRY(107),
	HNS3_RX_PTYPE_UNUSED_ENTRY(108),
	HNS3_RX_PTYPE_UNUSED_ENTRY(109),
	HNS3_RX_PTYPE_UNUSED_ENTRY(110),
	HNS3_RX_PTYPE_ENTRY(111, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(112, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(113, 0, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(114, 0, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(115, 0, NONE, IPV6),
	HNS3_RX_PTYPE_ENTRY(116, 0, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(117, 0, NONE, IPV6),
	HNS3_RX_PTYPE_ENTRY(118, 0, NONE, IPV6),
	HNS3_RX_PTYPE_ENTRY(119, 0, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_UNUSED_ENTRY(120),
	HNS3_RX_PTYPE_UNUSED_ENTRY(121),
	HNS3_RX_PTYPE_UNUSED_ENTRY(122),
	HNS3_RX_PTYPE_ENTRY(123, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(124, 0, COMPLETE, PARSE_FAIL),
	HNS3_RX_PTYPE_ENTRY(125, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(126, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(127, 1, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(128, 1, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(129, 1, UNNECESSARY, IPV4),
	HNS3_RX_PTYPE_ENTRY(130, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_ENTRY(131, 0, COMPLETE, IPV4),
	HNS3_RX_PTYPE_UNUSED_ENTRY(132),
	HNS3_RX_PTYPE_ENTRY(133, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(134, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(135, 1, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(136, 1, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(137, 1, UNNECESSARY, IPV6),
	HNS3_RX_PTYPE_ENTRY(138, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_ENTRY(139, 0, COMPLETE, IPV6),
	HNS3_RX_PTYPE_UNUSED_ENTRY(140),
	HNS3_RX_PTYPE_UNUSED_ENTRY(141),
	HNS3_RX_PTYPE_UNUSED_ENTRY(142),
	HNS3_RX_PTYPE_UNUSED_ENTRY(143),
	HNS3_RX_PTYPE_UNUSED_ENTRY(144),
	HNS3_RX_PTYPE_UNUSED_ENTRY(145),
	HNS3_RX_PTYPE_UNUSED_ENTRY(146),
	HNS3_RX_PTYPE_UNUSED_ENTRY(147),
	HNS3_RX_PTYPE_UNUSED_ENTRY(148),
	HNS3_RX_PTYPE_UNUSED_ENTRY(149),
	HNS3_RX_PTYPE_UNUSED_ENTRY(150),
	HNS3_RX_PTYPE_UNUSED_ENTRY(151),
	HNS3_RX_PTYPE_UNUSED_ENTRY(152),
	HNS3_RX_PTYPE_UNUSED_ENTRY(153),
	HNS3_RX_PTYPE_UNUSED_ENTRY(154),
	HNS3_RX_PTYPE_UNUSED_ENTRY(155),
	HNS3_RX_PTYPE_UNUSED_ENTRY(156),
	HNS3_RX_PTYPE_UNUSED_ENTRY(157),
	HNS3_RX_PTYPE_UNUSED_ENTRY(158),
	HNS3_RX_PTYPE_UNUSED_ENTRY(159),
	HNS3_RX_PTYPE_UNUSED_ENTRY(160),
	HNS3_RX_PTYPE_UNUSED_ENTRY(161),
	HNS3_RX_PTYPE_UNUSED_ENTRY(162),
	HNS3_RX_PTYPE_UNUSED_ENTRY(163),
	HNS3_RX_PTYPE_UNUSED_ENTRY(164),
	HNS3_RX_PTYPE_UNUSED_ENTRY(165),
	HNS3_RX_PTYPE_UNUSED_ENTRY(166),
	HNS3_RX_PTYPE_UNUSED_ENTRY(167),
	HNS3_RX_PTYPE_UNUSED_ENTRY(168),
	HNS3_RX_PTYPE_UNUSED_ENTRY(169),
	HNS3_RX_PTYPE_UNUSED_ENTRY(170),
	HNS3_RX_PTYPE_UNUSED_ENTRY(171),
	HNS3_RX_PTYPE_UNUSED_ENTRY(172),
	HNS3_RX_PTYPE_UNUSED_ENTRY(173),
	HNS3_RX_PTYPE_UNUSED_ENTRY(174),
	HNS3_RX_PTYPE_UNUSED_ENTRY(175),
	HNS3_RX_PTYPE_UNUSED_ENTRY(176),
	HNS3_RX_PTYPE_UNUSED_ENTRY(177),
	HNS3_RX_PTYPE_UNUSED_ENTRY(178),
	HNS3_RX_PTYPE_UNUSED_ENTRY(179),
	HNS3_RX_PTYPE_UNUSED_ENTRY(180),
	HNS3_RX_PTYPE_UNUSED_ENTRY(181),
	HNS3_RX_PTYPE_UNUSED_ENTRY(182),
	HNS3_RX_PTYPE_UNUSED_ENTRY(183),
	HNS3_RX_PTYPE_UNUSED_ENTRY(184),
	HNS3_RX_PTYPE_UNUSED_ENTRY(185),
	HNS3_RX_PTYPE_UNUSED_ENTRY(186),
	HNS3_RX_PTYPE_UNUSED_ENTRY(187),
	HNS3_RX_PTYPE_UNUSED_ENTRY(188),
	HNS3_RX_PTYPE_UNUSED_ENTRY(189),
	HNS3_RX_PTYPE_UNUSED_ENTRY(190),
	HNS3_RX_PTYPE_UNUSED_ENTRY(191),
	HNS3_RX_PTYPE_UNUSED_ENTRY(192),
	HNS3_RX_PTYPE_UNUSED_ENTRY(193),
	HNS3_RX_PTYPE_UNUSED_ENTRY(194),
	HNS3_RX_PTYPE_UNUSED_ENTRY(195),
	HNS3_RX_PTYPE_UNUSED_ENTRY(196),
	HNS3_RX_PTYPE_UNUSED_ENTRY(197),
	HNS3_RX_PTYPE_UNUSED_ENTRY(198),
	HNS3_RX_PTYPE_UNUSED_ENTRY(199),
	HNS3_RX_PTYPE_UNUSED_ENTRY(200),
	HNS3_RX_PTYPE_UNUSED_ENTRY(201),
	HNS3_RX_PTYPE_UNUSED_ENTRY(202),
	HNS3_RX_PTYPE_UNUSED_ENTRY(203),
	HNS3_RX_PTYPE_UNUSED_ENTRY(204),
	HNS3_RX_PTYPE_UNUSED_ENTRY(205),
	HNS3_RX_PTYPE_UNUSED_ENTRY(206),
	HNS3_RX_PTYPE_UNUSED_ENTRY(207),
	HNS3_RX_PTYPE_UNUSED_ENTRY(208),
	HNS3_RX_PTYPE_UNUSED_ENTRY(209),
	HNS3_RX_PTYPE_UNUSED_ENTRY(210),
	HNS3_RX_PTYPE_UNUSED_ENTRY(211),
	HNS3_RX_PTYPE_UNUSED_ENTRY(212),
	HNS3_RX_PTYPE_UNUSED_ENTRY(213),
	HNS3_RX_PTYPE_UNUSED_ENTRY(214),
	HNS3_RX_PTYPE_UNUSED_ENTRY(215),
	HNS3_RX_PTYPE_UNUSED_ENTRY(216),
	HNS3_RX_PTYPE_UNUSED_ENTRY(217),
	HNS3_RX_PTYPE_UNUSED_ENTRY(218),
	HNS3_RX_PTYPE_UNUSED_ENTRY(219),
	HNS3_RX_PTYPE_UNUSED_ENTRY(220),
	HNS3_RX_PTYPE_UNUSED_ENTRY(221),
	HNS3_RX_PTYPE_UNUSED_ENTRY(222),
	HNS3_RX_PTYPE_UNUSED_ENTRY(223),
	HNS3_RX_PTYPE_UNUSED_ENTRY(224),
	HNS3_RX_PTYPE_UNUSED_ENTRY(225),
	HNS3_RX_PTYPE_UNUSED_ENTRY(226),
	HNS3_RX_PTYPE_UNUSED_ENTRY(227),
	HNS3_RX_PTYPE_UNUSED_ENTRY(228),
	HNS3_RX_PTYPE_UNUSED_ENTRY(229),
	HNS3_RX_PTYPE_UNUSED_ENTRY(230),
	HNS3_RX_PTYPE_UNUSED_ENTRY(231),
	HNS3_RX_PTYPE_UNUSED_ENTRY(232),
	HNS3_RX_PTYPE_UNUSED_ENTRY(233),
	HNS3_RX_PTYPE_UNUSED_ENTRY(234),
	HNS3_RX_PTYPE_UNUSED_ENTRY(235),
	HNS3_RX_PTYPE_UNUSED_ENTRY(236),
	HNS3_RX_PTYPE_UNUSED_ENTRY(237),
	HNS3_RX_PTYPE_UNUSED_ENTRY(238),
	HNS3_RX_PTYPE_UNUSED_ENTRY(239),
	HNS3_RX_PTYPE_UNUSED_ENTRY(240),
	HNS3_RX_PTYPE_UNUSED_ENTRY(241),
	HNS3_RX_PTYPE_UNUSED_ENTRY(242),
	HNS3_RX_PTYPE_UNUSED_ENTRY(243),
	HNS3_RX_PTYPE_UNUSED_ENTRY(244),
	HNS3_RX_PTYPE_UNUSED_ENTRY(245),
	HNS3_RX_PTYPE_UNUSED_ENTRY(246),
	HNS3_RX_PTYPE_UNUSED_ENTRY(247),
	HNS3_RX_PTYPE_UNUSED_ENTRY(248),
	HNS3_RX_PTYPE_UNUSED_ENTRY(249),
	HNS3_RX_PTYPE_UNUSED_ENTRY(250),
	HNS3_RX_PTYPE_UNUSED_ENTRY(251),
	HNS3_RX_PTYPE_UNUSED_ENTRY(252),
	HNS3_RX_PTYPE_UNUSED_ENTRY(253),
	HNS3_RX_PTYPE_UNUSED_ENTRY(254),
	HNS3_RX_PTYPE_UNUSED_ENTRY(255),
};

#define HNS3_INVALID_PTYPE \
		ARRAY_SIZE(hns3_rx_ptype_tbl)

static irqreturn_t hns3_irq_handle(int irq, void *vector)
{
	struct hns3_enet_tqp_vector *tqp_vector = vector;

	napi_schedule_irqoff(&tqp_vector->napi);
	tqp_vector->event_cnt++;

	return IRQ_HANDLED;
}

static void hns3_nic_uninit_irq(struct hns3_nic_priv *priv)
{
	struct hns3_enet_tqp_vector *tqp_vectors;
	unsigned int i;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vectors = &priv->tqp_vector[i];

		if (tqp_vectors->irq_init_flag != HNS3_VECTOR_INITED)
			continue;

		/* clear the affinity mask */
		irq_set_affinity_hint(tqp_vectors->vector_irq, NULL);

		/* release the irq resource */
		free_irq(tqp_vectors->vector_irq, tqp_vectors);
		tqp_vectors->irq_init_flag = HNS3_VECTOR_NOT_INITED;
	}
}

static int hns3_nic_init_irq(struct hns3_nic_priv *priv)
{
	struct hns3_enet_tqp_vector *tqp_vectors;
	int txrx_int_idx = 0;
	int rx_int_idx = 0;
	int tx_int_idx = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vectors = &priv->tqp_vector[i];

		if (tqp_vectors->irq_init_flag == HNS3_VECTOR_INITED)
			continue;

		if (tqp_vectors->tx_group.ring && tqp_vectors->rx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN,
				 "%s-%s-%s-%d", hns3_driver_name,
				 pci_name(priv->ae_handle->pdev),
				 "TxRx", txrx_int_idx++);
			txrx_int_idx++;
		} else if (tqp_vectors->rx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN,
				 "%s-%s-%s-%d", hns3_driver_name,
				 pci_name(priv->ae_handle->pdev),
				 "Rx", rx_int_idx++);
		} else if (tqp_vectors->tx_group.ring) {
			snprintf(tqp_vectors->name, HNAE3_INT_NAME_LEN,
				 "%s-%s-%s-%d", hns3_driver_name,
				 pci_name(priv->ae_handle->pdev),
				 "Tx", tx_int_idx++);
		} else {
			/* Skip this unused q_vector */
			continue;
		}

		tqp_vectors->name[HNAE3_INT_NAME_LEN - 1] = '\0';

		irq_set_status_flags(tqp_vectors->vector_irq, IRQ_NOAUTOEN);
		ret = request_irq(tqp_vectors->vector_irq, hns3_irq_handle, 0,
				  tqp_vectors->name, tqp_vectors);
		if (ret) {
			netdev_err(priv->netdev, "request irq(%d) fail\n",
				   tqp_vectors->vector_irq);
			hns3_nic_uninit_irq(priv);
			return ret;
		}

		irq_set_affinity_hint(tqp_vectors->vector_irq,
				      &tqp_vectors->affinity_mask);

		tqp_vectors->irq_init_flag = HNS3_VECTOR_INITED;
	}

	return 0;
}

static void hns3_mask_vector_irq(struct hns3_enet_tqp_vector *tqp_vector,
				 u32 mask_en)
{
	writel(mask_en, tqp_vector->mask_addr);
}

static void hns3_vector_enable(struct hns3_enet_tqp_vector *tqp_vector)
{
	napi_enable(&tqp_vector->napi);
	enable_irq(tqp_vector->vector_irq);

	/* enable vector */
	hns3_mask_vector_irq(tqp_vector, 1);
}

static void hns3_vector_disable(struct hns3_enet_tqp_vector *tqp_vector)
{
	/* disable vector */
	hns3_mask_vector_irq(tqp_vector, 0);

	disable_irq(tqp_vector->vector_irq);
	napi_disable(&tqp_vector->napi);
	cancel_work_sync(&tqp_vector->rx_group.dim.work);
	cancel_work_sync(&tqp_vector->tx_group.dim.work);
}

void hns3_set_vector_coalesce_rl(struct hns3_enet_tqp_vector *tqp_vector,
				 u32 rl_value)
{
	u32 rl_reg = hns3_rl_usec_to_reg(rl_value);

	/* this defines the configuration for RL (Interrupt Rate Limiter).
	 * Rl defines rate of interrupts i.e. number of interrupts-per-second
	 * GL and RL(Rate Limiter) are 2 ways to acheive interrupt coalescing
	 */
	if (rl_reg > 0 && !tqp_vector->tx_group.coal.adapt_enable &&
	    !tqp_vector->rx_group.coal.adapt_enable)
		/* According to the hardware, the range of rl_reg is
		 * 0-59 and the unit is 4.
		 */
		rl_reg |=  HNS3_INT_RL_ENABLE_MASK;

	writel(rl_reg, tqp_vector->mask_addr + HNS3_VECTOR_RL_OFFSET);
}

void hns3_set_vector_coalesce_rx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value)
{
	u32 new_val;

	if (tqp_vector->rx_group.coal.unit_1us)
		new_val = gl_value | HNS3_INT_GL_1US;
	else
		new_val = hns3_gl_usec_to_reg(gl_value);

	writel(new_val, tqp_vector->mask_addr + HNS3_VECTOR_GL0_OFFSET);
}

void hns3_set_vector_coalesce_tx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value)
{
	u32 new_val;

	if (tqp_vector->tx_group.coal.unit_1us)
		new_val = gl_value | HNS3_INT_GL_1US;
	else
		new_val = hns3_gl_usec_to_reg(gl_value);

	writel(new_val, tqp_vector->mask_addr + HNS3_VECTOR_GL1_OFFSET);
}

void hns3_set_vector_coalesce_tx_ql(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 ql_value)
{
	writel(ql_value, tqp_vector->mask_addr + HNS3_VECTOR_TX_QL_OFFSET);
}

void hns3_set_vector_coalesce_rx_ql(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 ql_value)
{
	writel(ql_value, tqp_vector->mask_addr + HNS3_VECTOR_RX_QL_OFFSET);
}

static void hns3_vector_coalesce_init(struct hns3_enet_tqp_vector *tqp_vector,
				      struct hns3_nic_priv *priv)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(priv->ae_handle->pdev);
	struct hns3_enet_coalesce *tx_coal = &tqp_vector->tx_group.coal;
	struct hns3_enet_coalesce *rx_coal = &tqp_vector->rx_group.coal;
	struct hns3_enet_coalesce *ptx_coal = &priv->tx_coal;
	struct hns3_enet_coalesce *prx_coal = &priv->rx_coal;

	tx_coal->adapt_enable = ptx_coal->adapt_enable;
	rx_coal->adapt_enable = prx_coal->adapt_enable;

	tx_coal->int_gl = ptx_coal->int_gl;
	rx_coal->int_gl = prx_coal->int_gl;

	rx_coal->flow_level = prx_coal->flow_level;
	tx_coal->flow_level = ptx_coal->flow_level;

	/* device version above V3(include V3), GL can configure 1us
	 * unit, so uses 1us unit.
	 */
	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3) {
		tx_coal->unit_1us = 1;
		rx_coal->unit_1us = 1;
	}

	if (ae_dev->dev_specs.int_ql_max) {
		tx_coal->ql_enable = 1;
		rx_coal->ql_enable = 1;
		tx_coal->int_ql_max = ae_dev->dev_specs.int_ql_max;
		rx_coal->int_ql_max = ae_dev->dev_specs.int_ql_max;
		tx_coal->int_ql = ptx_coal->int_ql;
		rx_coal->int_ql = prx_coal->int_ql;
	}
}

static void
hns3_vector_coalesce_init_hw(struct hns3_enet_tqp_vector *tqp_vector,
			     struct hns3_nic_priv *priv)
{
	struct hns3_enet_coalesce *tx_coal = &tqp_vector->tx_group.coal;
	struct hns3_enet_coalesce *rx_coal = &tqp_vector->rx_group.coal;
	struct hnae3_handle *h = priv->ae_handle;

	hns3_set_vector_coalesce_tx_gl(tqp_vector, tx_coal->int_gl);
	hns3_set_vector_coalesce_rx_gl(tqp_vector, rx_coal->int_gl);
	hns3_set_vector_coalesce_rl(tqp_vector, h->kinfo.int_rl_setting);

	if (tx_coal->ql_enable)
		hns3_set_vector_coalesce_tx_ql(tqp_vector, tx_coal->int_ql);

	if (rx_coal->ql_enable)
		hns3_set_vector_coalesce_rx_ql(tqp_vector, rx_coal->int_ql);
}

static int hns3_nic_set_real_num_queue(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	struct hnae3_tc_info *tc_info = &kinfo->tc_info;
	unsigned int queue_size = kinfo->num_tqps;
	int i, ret;

	if (tc_info->num_tc <= 1 && !tc_info->mqprio_active) {
		netdev_reset_tc(netdev);
	} else {
		ret = netdev_set_num_tc(netdev, tc_info->num_tc);
		if (ret) {
			netdev_err(netdev,
				   "netdev_set_num_tc fail, ret=%d!\n", ret);
			return ret;
		}

		for (i = 0; i < tc_info->num_tc; i++)
			netdev_set_tc_queue(netdev, i, tc_info->tqp_count[i],
					    tc_info->tqp_offset[i]);
	}

	ret = netif_set_real_num_tx_queues(netdev, queue_size);
	if (ret) {
		netdev_err(netdev,
			   "netif_set_real_num_tx_queues fail, ret=%d!\n", ret);
		return ret;
	}

	ret = netif_set_real_num_rx_queues(netdev, queue_size);
	if (ret) {
		netdev_err(netdev,
			   "netif_set_real_num_rx_queues fail, ret=%d!\n", ret);
		return ret;
	}

	return 0;
}

u16 hns3_get_max_available_channels(struct hnae3_handle *h)
{
	u16 alloc_tqps, max_rss_size, rss_size;

	h->ae_algo->ops->get_tqps_and_rss_info(h, &alloc_tqps, &max_rss_size);
	rss_size = alloc_tqps / h->kinfo.tc_info.num_tc;

	return min_t(u16, rss_size, max_rss_size);
}

static void hns3_tqp_enable(struct hnae3_queue *tqp)
{
	u32 rcb_reg;

	rcb_reg = hns3_read_dev(tqp, HNS3_RING_EN_REG);
	rcb_reg |= BIT(HNS3_RING_EN_B);
	hns3_write_dev(tqp, HNS3_RING_EN_REG, rcb_reg);
}

static void hns3_tqp_disable(struct hnae3_queue *tqp)
{
	u32 rcb_reg;

	rcb_reg = hns3_read_dev(tqp, HNS3_RING_EN_REG);
	rcb_reg &= ~BIT(HNS3_RING_EN_B);
	hns3_write_dev(tqp, HNS3_RING_EN_REG, rcb_reg);
}

static void hns3_free_rx_cpu_rmap(struct net_device *netdev)
{
#ifdef CONFIG_RFS_ACCEL
	free_irq_cpu_rmap(netdev->rx_cpu_rmap);
	netdev->rx_cpu_rmap = NULL;
#endif
}

static int hns3_set_rx_cpu_rmap(struct net_device *netdev)
{
#ifdef CONFIG_RFS_ACCEL
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hns3_enet_tqp_vector *tqp_vector;
	int i, ret;

	if (!netdev->rx_cpu_rmap) {
		netdev->rx_cpu_rmap = alloc_irq_cpu_rmap(priv->vector_num);
		if (!netdev->rx_cpu_rmap)
			return -ENOMEM;
	}

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		ret = irq_cpu_rmap_add(netdev->rx_cpu_rmap,
				       tqp_vector->vector_irq);
		if (ret) {
			hns3_free_rx_cpu_rmap(netdev);
			return ret;
		}
	}
#endif
	return 0;
}

static int hns3_nic_net_up(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	int i, j;
	int ret;

	ret = hns3_nic_reset_all_ring(h);
	if (ret)
		return ret;

	clear_bit(HNS3_NIC_STATE_DOWN, &priv->state);

	/* enable the vectors */
	for (i = 0; i < priv->vector_num; i++)
		hns3_vector_enable(&priv->tqp_vector[i]);

	/* enable rcb */
	for (j = 0; j < h->kinfo.num_tqps; j++)
		hns3_tqp_enable(h->kinfo.tqp[j]);

	/* start the ae_dev */
	ret = h->ae_algo->ops->start ? h->ae_algo->ops->start(h) : 0;
	if (ret) {
		set_bit(HNS3_NIC_STATE_DOWN, &priv->state);
		while (j--)
			hns3_tqp_disable(h->kinfo.tqp[j]);

		for (j = i - 1; j >= 0; j--)
			hns3_vector_disable(&priv->tqp_vector[j]);
	}

	return ret;
}

static void hns3_config_xps(struct hns3_nic_priv *priv)
{
	int i;

	for (i = 0; i < priv->vector_num; i++) {
		struct hns3_enet_tqp_vector *tqp_vector = &priv->tqp_vector[i];
		struct hns3_enet_ring *ring = tqp_vector->tx_group.ring;

		while (ring) {
			int ret;

			ret = netif_set_xps_queue(priv->netdev,
						  &tqp_vector->affinity_mask,
						  ring->tqp->tqp_index);
			if (ret)
				netdev_warn(priv->netdev,
					    "set xps queue failed: %d", ret);

			ring = ring->next;
		}
	}
}

static int hns3_nic_net_open(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo;
	int i, ret;

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	if (!test_bit(HNS3_NIC_STATE_DOWN, &priv->state)) {
		netdev_warn(netdev, "net open repeatedly!\n");
		return 0;
	}

	netif_carrier_off(netdev);

	ret = hns3_nic_set_real_num_queue(netdev);
	if (ret)
		return ret;

	ret = hns3_nic_net_up(netdev);
	if (ret) {
		netdev_err(netdev, "net up fail, ret=%d!\n", ret);
		return ret;
	}

	kinfo = &h->kinfo;
	for (i = 0; i < HNAE3_MAX_USER_PRIO; i++)
		netdev_set_prio_tc_map(netdev, i, kinfo->tc_info.prio_tc[i]);

	if (h->ae_algo->ops->set_timer_task)
		h->ae_algo->ops->set_timer_task(priv->ae_handle, true);

	hns3_config_xps(priv);

	netif_dbg(h, drv, netdev, "net open\n");

	return 0;
}

static void hns3_reset_tx_queue(struct hnae3_handle *h)
{
	struct net_device *ndev = h->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct netdev_queue *dev_queue;
	u32 i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		dev_queue = netdev_get_tx_queue(ndev,
						priv->ring[i].queue_index);
		netdev_tx_reset_queue(dev_queue);
	}
}

static void hns3_nic_net_down(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);
	const struct hnae3_ae_ops *ops;
	int i;

	/* disable vectors */
	for (i = 0; i < priv->vector_num; i++)
		hns3_vector_disable(&priv->tqp_vector[i]);

	/* disable rcb */
	for (i = 0; i < h->kinfo.num_tqps; i++)
		hns3_tqp_disable(h->kinfo.tqp[i]);

	/* stop ae_dev */
	ops = priv->ae_handle->ae_algo->ops;
	if (ops->stop)
		ops->stop(priv->ae_handle);

	/* delay ring buffer clearing to hns3_reset_notify_uninit_enet
	 * during reset process, because driver may not be able
	 * to disable the ring through firmware when downing the netdev.
	 */
	if (!hns3_nic_resetting(netdev))
		hns3_clear_all_ring(priv->ae_handle, false);

	hns3_reset_tx_queue(priv->ae_handle);
}

static int hns3_nic_net_stop(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (test_and_set_bit(HNS3_NIC_STATE_DOWN, &priv->state))
		return 0;

	netif_dbg(h, drv, netdev, "net stop\n");

	if (h->ae_algo->ops->set_timer_task)
		h->ae_algo->ops->set_timer_task(priv->ae_handle, false);

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	hns3_nic_net_down(netdev);

	return 0;
}

static int hns3_nic_uc_sync(struct net_device *netdev,
			    const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->add_uc_addr)
		return h->ae_algo->ops->add_uc_addr(h, addr);

	return 0;
}

static int hns3_nic_uc_unsync(struct net_device *netdev,
			      const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	/* need ignore the request of removing device address, because
	 * we store the device address and other addresses of uc list
	 * in the function's mac filter list.
	 */
	if (ether_addr_equal(addr, netdev->dev_addr))
		return 0;

	if (h->ae_algo->ops->rm_uc_addr)
		return h->ae_algo->ops->rm_uc_addr(h, addr);

	return 0;
}

static int hns3_nic_mc_sync(struct net_device *netdev,
			    const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->add_mc_addr)
		return h->ae_algo->ops->add_mc_addr(h, addr);

	return 0;
}

static int hns3_nic_mc_unsync(struct net_device *netdev,
			      const unsigned char *addr)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->rm_mc_addr)
		return h->ae_algo->ops->rm_mc_addr(h, addr);

	return 0;
}

static u8 hns3_get_netdev_flags(struct net_device *netdev)
{
	u8 flags = 0;

	if (netdev->flags & IFF_PROMISC)
		flags = HNAE3_USER_UPE | HNAE3_USER_MPE | HNAE3_BPE;
	else if (netdev->flags & IFF_ALLMULTI)
		flags = HNAE3_USER_MPE;

	return flags;
}

static void hns3_nic_set_rx_mode(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	u8 new_flags;

	new_flags = hns3_get_netdev_flags(netdev);

	__dev_uc_sync(netdev, hns3_nic_uc_sync, hns3_nic_uc_unsync);
	__dev_mc_sync(netdev, hns3_nic_mc_sync, hns3_nic_mc_unsync);

	/* User mode Promisc mode enable and vlan filtering is disabled to
	 * let all packets in.
	 */
	h->netdev_flags = new_flags;
	hns3_request_update_promisc_mode(h);
}

void hns3_request_update_promisc_mode(struct hnae3_handle *handle)
{
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;

	if (ops->request_update_promisc_mode)
		ops->request_update_promisc_mode(handle);
}

static u32 hns3_tx_spare_space(struct hns3_enet_ring *ring)
{
	struct hns3_tx_spare *tx_spare = ring->tx_spare;
	u32 ntc, ntu;

	/* This smp_load_acquire() pairs with smp_store_release() in
	 * hns3_tx_spare_update() called in tx desc cleaning process.
	 */
	ntc = smp_load_acquire(&tx_spare->last_to_clean);
	ntu = tx_spare->next_to_use;

	if (ntc > ntu)
		return ntc - ntu - 1;

	/* The free tx buffer is divided into two part, so pick the
	 * larger one.
	 */
	return max(ntc, tx_spare->len - ntu) - 1;
}

static void hns3_tx_spare_update(struct hns3_enet_ring *ring)
{
	struct hns3_tx_spare *tx_spare = ring->tx_spare;

	if (!tx_spare ||
	    tx_spare->last_to_clean == tx_spare->next_to_clean)
		return;

	/* This smp_store_release() pairs with smp_load_acquire() in
	 * hns3_tx_spare_space() called in xmit process.
	 */
	smp_store_release(&tx_spare->last_to_clean,
			  tx_spare->next_to_clean);
}

static bool hns3_can_use_tx_bounce(struct hns3_enet_ring *ring,
				   struct sk_buff *skb,
				   u32 space)
{
	u32 len = skb->len <= ring->tx_copybreak ? skb->len :
				skb_headlen(skb);

	if (len > ring->tx_copybreak)
		return false;

	if (ALIGN(len, dma_get_cache_alignment()) > space) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.tx_spare_full++;
		u64_stats_update_end(&ring->syncp);
		return false;
	}

	return true;
}

static bool hns3_can_use_tx_sgl(struct hns3_enet_ring *ring,
				struct sk_buff *skb,
				u32 space)
{
	if (skb->len <= ring->tx_copybreak || !tx_sgl ||
	    (!skb_has_frag_list(skb) &&
	     skb_shinfo(skb)->nr_frags < tx_sgl))
		return false;

	if (space < HNS3_MAX_SGL_SIZE) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.tx_spare_full++;
		u64_stats_update_end(&ring->syncp);
		return false;
	}

	return true;
}

static void hns3_init_tx_spare_buffer(struct hns3_enet_ring *ring)
{
	struct hns3_tx_spare *tx_spare;
	struct page *page;
	u32 alloc_size;
	dma_addr_t dma;
	int order;

	alloc_size = tx_spare_buf_size ? tx_spare_buf_size :
		     ring->tqp->handle->kinfo.tx_spare_buf_size;
	if (!alloc_size)
		return;

	order = get_order(alloc_size);
	tx_spare = devm_kzalloc(ring_to_dev(ring), sizeof(*tx_spare),
				GFP_KERNEL);
	if (!tx_spare) {
		/* The driver still work without the tx spare buffer */
		dev_warn(ring_to_dev(ring), "failed to allocate hns3_tx_spare\n");
		return;
	}

	page = alloc_pages_node(dev_to_node(ring_to_dev(ring)),
				GFP_KERNEL, order);
	if (!page) {
		dev_warn(ring_to_dev(ring), "failed to allocate tx spare pages\n");
		devm_kfree(ring_to_dev(ring), tx_spare);
		return;
	}

	dma = dma_map_page(ring_to_dev(ring), page, 0,
			   PAGE_SIZE << order, DMA_TO_DEVICE);
	if (dma_mapping_error(ring_to_dev(ring), dma)) {
		dev_warn(ring_to_dev(ring), "failed to map pages for tx spare\n");
		put_page(page);
		devm_kfree(ring_to_dev(ring), tx_spare);
		return;
	}

	tx_spare->dma = dma;
	tx_spare->buf = page_address(page);
	tx_spare->len = PAGE_SIZE << order;
	ring->tx_spare = tx_spare;
}

/* Use hns3_tx_spare_space() to make sure there is enough buffer
 * before calling below function to allocate tx buffer.
 */
static void *hns3_tx_spare_alloc(struct hns3_enet_ring *ring,
				 unsigned int size, dma_addr_t *dma,
				 u32 *cb_len)
{
	struct hns3_tx_spare *tx_spare = ring->tx_spare;
	u32 ntu = tx_spare->next_to_use;

	size = ALIGN(size, dma_get_cache_alignment());
	*cb_len = size;

	/* Tx spare buffer wraps back here because the end of
	 * freed tx buffer is not enough.
	 */
	if (ntu + size > tx_spare->len) {
		*cb_len += (tx_spare->len - ntu);
		ntu = 0;
	}

	tx_spare->next_to_use = ntu + size;
	if (tx_spare->next_to_use == tx_spare->len)
		tx_spare->next_to_use = 0;

	*dma = tx_spare->dma + ntu;

	return tx_spare->buf + ntu;
}

static void hns3_tx_spare_rollback(struct hns3_enet_ring *ring, u32 len)
{
	struct hns3_tx_spare *tx_spare = ring->tx_spare;

	if (len > tx_spare->next_to_use) {
		len -= tx_spare->next_to_use;
		tx_spare->next_to_use = tx_spare->len - len;
	} else {
		tx_spare->next_to_use -= len;
	}
}

static void hns3_tx_spare_reclaim_cb(struct hns3_enet_ring *ring,
				     struct hns3_desc_cb *cb)
{
	struct hns3_tx_spare *tx_spare = ring->tx_spare;
	u32 ntc = tx_spare->next_to_clean;
	u32 len = cb->length;

	tx_spare->next_to_clean += len;

	if (tx_spare->next_to_clean >= tx_spare->len) {
		tx_spare->next_to_clean -= tx_spare->len;

		if (tx_spare->next_to_clean) {
			ntc = 0;
			len = tx_spare->next_to_clean;
		}
	}

	/* This tx spare buffer is only really reclaimed after calling
	 * hns3_tx_spare_update(), so it is still safe to use the info in
	 * the tx buffer to do the dma sync or sg unmapping after
	 * tx_spare->next_to_clean is moved forword.
	 */
	if (cb->type & (DESC_TYPE_BOUNCE_HEAD | DESC_TYPE_BOUNCE_ALL)) {
		dma_addr_t dma = tx_spare->dma + ntc;

		dma_sync_single_for_cpu(ring_to_dev(ring), dma, len,
					DMA_TO_DEVICE);
	} else {
		struct sg_table *sgt = tx_spare->buf + ntc;

		dma_unmap_sg(ring_to_dev(ring), sgt->sgl, sgt->orig_nents,
			     DMA_TO_DEVICE);
	}
}

static int hns3_set_tso(struct sk_buff *skb, u32 *paylen_fdop_ol4cs,
			u16 *mss, u32 *type_cs_vlan_tso, u32 *send_bytes)
{
	u32 l4_offset, hdr_len;
	union l3_hdr_info l3;
	union l4_hdr_info l4;
	u32 l4_paylen;
	int ret;

	if (!skb_is_gso(skb))
		return 0;

	ret = skb_cow_head(skb, 0);
	if (unlikely(ret < 0))
		return ret;

	l3.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* Software should clear the IPv4's checksum field when tso is
	 * needed.
	 */
	if (l3.v4->version == 4)
		l3.v4->check = 0;

	/* tunnel packet */
	if (skb_shinfo(skb)->gso_type & (SKB_GSO_GRE |
					 SKB_GSO_GRE_CSUM |
					 SKB_GSO_UDP_TUNNEL |
					 SKB_GSO_UDP_TUNNEL_CSUM)) {
		/* reset l3&l4 pointers from outer to inner headers */
		l3.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);

		/* Software should clear the IPv4's checksum field when
		 * tso is needed.
		 */
		if (l3.v4->version == 4)
			l3.v4->check = 0;
	}

	/* normal or tunnel packet */
	l4_offset = l4.hdr - skb->data;

	/* remove payload length from inner pseudo checksum when tso */
	l4_paylen = skb->len - l4_offset;

	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
		hdr_len = sizeof(*l4.udp) + l4_offset;
		csum_replace_by_diff(&l4.udp->check,
				     (__force __wsum)htonl(l4_paylen));
	} else {
		hdr_len = (l4.tcp->doff << 2) + l4_offset;
		csum_replace_by_diff(&l4.tcp->check,
				     (__force __wsum)htonl(l4_paylen));
	}

	*send_bytes = (skb_shinfo(skb)->gso_segs - 1) * hdr_len + skb->len;

	/* find the txbd field values */
	*paylen_fdop_ol4cs = skb->len - hdr_len;
	hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_TSO_B, 1);

	/* offload outer UDP header checksum */
	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM)
		hns3_set_field(*paylen_fdop_ol4cs, HNS3_TXD_OL4CS_B, 1);

	/* get MSS for TSO */
	*mss = skb_shinfo(skb)->gso_size;

	trace_hns3_tso(skb);

	return 0;
}

static int hns3_get_l4_protocol(struct sk_buff *skb, u8 *ol4_proto,
				u8 *il4_proto)
{
	union l3_hdr_info l3;
	unsigned char *l4_hdr;
	unsigned char *exthdr;
	u8 l4_proto_tmp;
	__be16 frag_off;

	/* find outer header point */
	l3.hdr = skb_network_header(skb);
	l4_hdr = skb_transport_header(skb);

	if (skb->protocol == htons(ETH_P_IPV6)) {
		exthdr = l3.hdr + sizeof(*l3.v6);
		l4_proto_tmp = l3.v6->nexthdr;
		if (l4_hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 &l4_proto_tmp, &frag_off);
	} else if (skb->protocol == htons(ETH_P_IP)) {
		l4_proto_tmp = l3.v4->protocol;
	} else {
		return -EINVAL;
	}

	*ol4_proto = l4_proto_tmp;

	/* tunnel packet */
	if (!skb->encapsulation) {
		*il4_proto = 0;
		return 0;
	}

	/* find inner header point */
	l3.hdr = skb_inner_network_header(skb);
	l4_hdr = skb_inner_transport_header(skb);

	if (l3.v6->version == 6) {
		exthdr = l3.hdr + sizeof(*l3.v6);
		l4_proto_tmp = l3.v6->nexthdr;
		if (l4_hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 &l4_proto_tmp, &frag_off);
	} else if (l3.v4->version == 4) {
		l4_proto_tmp = l3.v4->protocol;
	}

	*il4_proto = l4_proto_tmp;

	return 0;
}

/* when skb->encapsulation is 0, skb->ip_summed is CHECKSUM_PARTIAL
 * and it is udp packet, which has a dest port as the IANA assigned.
 * the hardware is expected to do the checksum offload, but the
 * hardware will not do the checksum offload when udp dest port is
 * 4789, 4790 or 6081.
 */
static bool hns3_tunnel_csum_bug(struct sk_buff *skb)
{
	struct hns3_nic_priv *priv = netdev_priv(skb->dev);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(priv->ae_handle->pdev);
	union l4_hdr_info l4;

	/* device version above V3(include V3), the hardware can
	 * do this checksum offload.
	 */
	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3)
		return false;

	l4.hdr = skb_transport_header(skb);

	if (!(!skb->encapsulation &&
	      (l4.udp->dest == htons(IANA_VXLAN_UDP_PORT) ||
	      l4.udp->dest == htons(GENEVE_UDP_PORT) ||
	      l4.udp->dest == htons(4790))))
		return false;

	return true;
}

static void hns3_set_outer_l2l3l4(struct sk_buff *skb, u8 ol4_proto,
				  u32 *ol_type_vlan_len_msec)
{
	u32 l2_len, l3_len, l4_len;
	unsigned char *il2_hdr;
	union l3_hdr_info l3;
	union l4_hdr_info l4;

	l3.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* compute OL2 header size, defined in 2 Bytes */
	l2_len = l3.hdr - skb->data;
	hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L2LEN_S, l2_len >> 1);

	/* compute OL3 header size, defined in 4 Bytes */
	l3_len = l4.hdr - l3.hdr;
	hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L3LEN_S, l3_len >> 2);

	il2_hdr = skb_inner_mac_header(skb);
	/* compute OL4 header size, defined in 4 Bytes */
	l4_len = il2_hdr - l4.hdr;
	hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_L4LEN_S, l4_len >> 2);

	/* define outer network header type */
	if (skb->protocol == htons(ETH_P_IP)) {
		if (skb_is_gso(skb))
			hns3_set_field(*ol_type_vlan_len_msec,
				       HNS3_TXD_OL3T_S,
				       HNS3_OL3T_IPV4_CSUM);
		else
			hns3_set_field(*ol_type_vlan_len_msec,
				       HNS3_TXD_OL3T_S,
				       HNS3_OL3T_IPV4_NO_CSUM);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_OL3T_S,
			       HNS3_OL3T_IPV6);
	}

	if (ol4_proto == IPPROTO_UDP)
		hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_TUNTYPE_S,
			       HNS3_TUN_MAC_IN_UDP);
	else if (ol4_proto == IPPROTO_GRE)
		hns3_set_field(*ol_type_vlan_len_msec, HNS3_TXD_TUNTYPE_S,
			       HNS3_TUN_NVGRE);
}

static int hns3_set_l2l3l4(struct sk_buff *skb, u8 ol4_proto,
			   u8 il4_proto, u32 *type_cs_vlan_tso,
			   u32 *ol_type_vlan_len_msec)
{
	unsigned char *l2_hdr = skb->data;
	u32 l4_proto = ol4_proto;
	union l4_hdr_info l4;
	union l3_hdr_info l3;
	u32 l2_len, l3_len;

	l4.hdr = skb_transport_header(skb);
	l3.hdr = skb_network_header(skb);

	/* handle encapsulation skb */
	if (skb->encapsulation) {
		/* If this is a not UDP/GRE encapsulation skb */
		if (!(ol4_proto == IPPROTO_UDP || ol4_proto == IPPROTO_GRE)) {
			/* drop the skb tunnel packet if hardware don't support,
			 * because hardware can't calculate csum when TSO.
			 */
			if (skb_is_gso(skb))
				return -EDOM;

			/* the stack computes the IP header already,
			 * driver calculate l4 checksum when not TSO.
			 */
			return skb_checksum_help(skb);
		}

		hns3_set_outer_l2l3l4(skb, ol4_proto, ol_type_vlan_len_msec);

		/* switch to inner header */
		l2_hdr = skb_inner_mac_header(skb);
		l3.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);
		l4_proto = il4_proto;
	}

	if (l3.v4->version == 4) {
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3T_S,
			       HNS3_L3T_IPV4);

		/* the stack computes the IP header already, the only time we
		 * need the hardware to recompute it is in the case of TSO.
		 */
		if (skb_is_gso(skb))
			hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3CS_B, 1);
	} else if (l3.v6->version == 6) {
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3T_S,
			       HNS3_L3T_IPV6);
	}

	/* compute inner(/normal) L2 header size, defined in 2 Bytes */
	l2_len = l3.hdr - l2_hdr;
	hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L2LEN_S, l2_len >> 1);

	/* compute inner(/normal) L3 header size, defined in 4 Bytes */
	l3_len = l4.hdr - l3.hdr;
	hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L3LEN_S, l3_len >> 2);

	/* compute inner(/normal) L4 header size, defined in 4 Bytes */
	switch (l4_proto) {
	case IPPROTO_TCP:
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4T_S,
			       HNS3_L4T_TCP);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_S,
			       l4.tcp->doff);
		break;
	case IPPROTO_UDP:
		if (hns3_tunnel_csum_bug(skb)) {
			int ret = skb_put_padto(skb, HNS3_MIN_TUN_PKT_LEN);

			return ret ? ret : skb_checksum_help(skb);
		}

		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4T_S,
			       HNS3_L4T_UDP);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_S,
			       (sizeof(struct udphdr) >> 2));
		break;
	case IPPROTO_SCTP:
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4CS_B, 1);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4T_S,
			       HNS3_L4T_SCTP);
		hns3_set_field(*type_cs_vlan_tso, HNS3_TXD_L4LEN_S,
			       (sizeof(struct sctphdr) >> 2));
		break;
	default:
		/* drop the skb tunnel packet if hardware don't support,
		 * because hardware can't calculate csum when TSO.
		 */
		if (skb_is_gso(skb))
			return -EDOM;

		/* the stack computes the IP header already,
		 * driver calculate l4 checksum when not TSO.
		 */
		return skb_checksum_help(skb);
	}

	return 0;
}

static int hns3_handle_vtags(struct hns3_enet_ring *tx_ring,
			     struct sk_buff *skb)
{
	struct hnae3_handle *handle = tx_ring->tqp->handle;
	struct hnae3_ae_dev *ae_dev;
	struct vlan_ethhdr *vhdr;
	int rc;

	if (!(skb->protocol == htons(ETH_P_8021Q) ||
	      skb_vlan_tag_present(skb)))
		return 0;

	/* For HW limitation on HNAE3_DEVICE_VERSION_V2, if port based insert
	 * VLAN enabled, only one VLAN header is allowed in skb, otherwise it
	 * will cause RAS error.
	 */
	ae_dev = pci_get_drvdata(handle->pdev);
	if (unlikely(skb_vlan_tagged_multi(skb) &&
		     ae_dev->dev_version <= HNAE3_DEVICE_VERSION_V2 &&
		     handle->port_base_vlan_state ==
		     HNAE3_PORT_BASE_VLAN_ENABLE))
		return -EINVAL;

	if (skb->protocol == htons(ETH_P_8021Q) &&
	    !(handle->kinfo.netdev->features & NETIF_F_HW_VLAN_CTAG_TX)) {
		/* When HW VLAN acceleration is turned off, and the stack
		 * sets the protocol to 802.1q, the driver just need to
		 * set the protocol to the encapsulated ethertype.
		 */
		skb->protocol = vlan_get_protocol(skb);
		return 0;
	}

	if (skb_vlan_tag_present(skb)) {
		/* Based on hw strategy, use out_vtag in two layer tag case,
		 * and use inner_vtag in one tag case.
		 */
		if (skb->protocol == htons(ETH_P_8021Q) &&
		    handle->port_base_vlan_state ==
		    HNAE3_PORT_BASE_VLAN_DISABLE)
			rc = HNS3_OUTER_VLAN_TAG;
		else
			rc = HNS3_INNER_VLAN_TAG;

		skb->protocol = vlan_get_protocol(skb);
		return rc;
	}

	rc = skb_cow_head(skb, 0);
	if (unlikely(rc < 0))
		return rc;

	vhdr = (struct vlan_ethhdr *)skb->data;
	vhdr->h_vlan_TCI |= cpu_to_be16((skb->priority << VLAN_PRIO_SHIFT)
					 & VLAN_PRIO_MASK);

	skb->protocol = vlan_get_protocol(skb);
	return 0;
}

/* check if the hardware is capable of checksum offloading */
static bool hns3_check_hw_tx_csum(struct sk_buff *skb)
{
	struct hns3_nic_priv *priv = netdev_priv(skb->dev);

	/* Kindly note, due to backward compatibility of the TX descriptor,
	 * HW checksum of the non-IP packets and GSO packets is handled at
	 * different place in the following code
	 */
	if (skb_csum_is_sctp(skb) || skb_is_gso(skb) ||
	    !test_bit(HNS3_NIC_STATE_HW_TX_CSUM_ENABLE, &priv->state))
		return false;

	return true;
}

static int hns3_fill_skb_desc(struct hns3_enet_ring *ring,
			      struct sk_buff *skb, struct hns3_desc *desc,
			      struct hns3_desc_cb *desc_cb)
{
	u32 ol_type_vlan_len_msec = 0;
	u32 paylen_ol4cs = skb->len;
	u32 type_cs_vlan_tso = 0;
	u16 mss_hw_csum = 0;
	u16 inner_vtag = 0;
	u16 out_vtag = 0;
	int ret;

	ret = hns3_handle_vtags(ring, skb);
	if (unlikely(ret < 0)) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.tx_vlan_err++;
		u64_stats_update_end(&ring->syncp);
		return ret;
	} else if (ret == HNS3_INNER_VLAN_TAG) {
		inner_vtag = skb_vlan_tag_get(skb);
		inner_vtag |= (skb->priority << VLAN_PRIO_SHIFT) &
				VLAN_PRIO_MASK;
		hns3_set_field(type_cs_vlan_tso, HNS3_TXD_VLAN_B, 1);
	} else if (ret == HNS3_OUTER_VLAN_TAG) {
		out_vtag = skb_vlan_tag_get(skb);
		out_vtag |= (skb->priority << VLAN_PRIO_SHIFT) &
				VLAN_PRIO_MASK;
		hns3_set_field(ol_type_vlan_len_msec, HNS3_TXD_OVLAN_B,
			       1);
	}

	desc_cb->send_bytes = skb->len;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		u8 ol4_proto, il4_proto;

		if (hns3_check_hw_tx_csum(skb)) {
			/* set checksum start and offset, defined in 2 Bytes */
			hns3_set_field(type_cs_vlan_tso, HNS3_TXD_CSUM_START_S,
				       skb_checksum_start_offset(skb) >> 1);
			hns3_set_field(ol_type_vlan_len_msec,
				       HNS3_TXD_CSUM_OFFSET_S,
				       skb->csum_offset >> 1);
			mss_hw_csum |= BIT(HNS3_TXD_HW_CS_B);
			goto out_hw_tx_csum;
		}

		skb_reset_mac_len(skb);

		ret = hns3_get_l4_protocol(skb, &ol4_proto, &il4_proto);
		if (unlikely(ret < 0)) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.tx_l4_proto_err++;
			u64_stats_update_end(&ring->syncp);
			return ret;
		}

		ret = hns3_set_l2l3l4(skb, ol4_proto, il4_proto,
				      &type_cs_vlan_tso,
				      &ol_type_vlan_len_msec);
		if (unlikely(ret < 0)) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.tx_l2l3l4_err++;
			u64_stats_update_end(&ring->syncp);
			return ret;
		}

		ret = hns3_set_tso(skb, &paylen_ol4cs, &mss_hw_csum,
				   &type_cs_vlan_tso, &desc_cb->send_bytes);
		if (unlikely(ret < 0)) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.tx_tso_err++;
			u64_stats_update_end(&ring->syncp);
			return ret;
		}
	}

out_hw_tx_csum:
	/* Set txbd */
	desc->tx.ol_type_vlan_len_msec =
		cpu_to_le32(ol_type_vlan_len_msec);
	desc->tx.type_cs_vlan_tso_len = cpu_to_le32(type_cs_vlan_tso);
	desc->tx.paylen_ol4cs = cpu_to_le32(paylen_ol4cs);
	desc->tx.mss_hw_csum = cpu_to_le16(mss_hw_csum);
	desc->tx.vlan_tag = cpu_to_le16(inner_vtag);
	desc->tx.outer_vlan_tag = cpu_to_le16(out_vtag);

	return 0;
}

static int hns3_fill_desc(struct hns3_enet_ring *ring, dma_addr_t dma,
			  unsigned int size)
{
#define HNS3_LIKELY_BD_NUM	1

	struct hns3_desc *desc = &ring->desc[ring->next_to_use];
	unsigned int frag_buf_num;
	int k, sizeoflast;

	if (likely(size <= HNS3_MAX_BD_SIZE)) {
		desc->addr = cpu_to_le64(dma);
		desc->tx.send_size = cpu_to_le16(size);
		desc->tx.bdtp_fe_sc_vld_ra_ri =
			cpu_to_le16(BIT(HNS3_TXD_VLD_B));

		trace_hns3_tx_desc(ring, ring->next_to_use);
		ring_ptr_move_fw(ring, next_to_use);
		return HNS3_LIKELY_BD_NUM;
	}

	frag_buf_num = hns3_tx_bd_count(size);
	sizeoflast = size % HNS3_MAX_BD_SIZE;
	sizeoflast = sizeoflast ? sizeoflast : HNS3_MAX_BD_SIZE;

	/* When frag size is bigger than hardware limit, split this frag */
	for (k = 0; k < frag_buf_num; k++) {
		/* now, fill the descriptor */
		desc->addr = cpu_to_le64(dma + HNS3_MAX_BD_SIZE * k);
		desc->tx.send_size = cpu_to_le16((k == frag_buf_num - 1) ?
				     (u16)sizeoflast : (u16)HNS3_MAX_BD_SIZE);
		desc->tx.bdtp_fe_sc_vld_ra_ri =
				cpu_to_le16(BIT(HNS3_TXD_VLD_B));

		trace_hns3_tx_desc(ring, ring->next_to_use);
		/* move ring pointer to next */
		ring_ptr_move_fw(ring, next_to_use);

		desc = &ring->desc[ring->next_to_use];
	}

	return frag_buf_num;
}

static int hns3_map_and_fill_desc(struct hns3_enet_ring *ring, void *priv,
				  unsigned int type)
{
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct device *dev = ring_to_dev(ring);
	unsigned int size;
	dma_addr_t dma;

	if (type & (DESC_TYPE_FRAGLIST_SKB | DESC_TYPE_SKB)) {
		struct sk_buff *skb = (struct sk_buff *)priv;

		size = skb_headlen(skb);
		if (!size)
			return 0;

		dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);
	} else if (type & DESC_TYPE_BOUNCE_HEAD) {
		/* Head data has been filled in hns3_handle_tx_bounce(),
		 * just return 0 here.
		 */
		return 0;
	} else {
		skb_frag_t *frag = (skb_frag_t *)priv;

		size = skb_frag_size(frag);
		if (!size)
			return 0;

		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
	}

	if (unlikely(dma_mapping_error(dev, dma))) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.sw_err_cnt++;
		u64_stats_update_end(&ring->syncp);
		return -ENOMEM;
	}

	desc_cb->priv = priv;
	desc_cb->length = size;
	desc_cb->dma = dma;
	desc_cb->type = type;

	return hns3_fill_desc(ring, dma, size);
}

static unsigned int hns3_skb_bd_num(struct sk_buff *skb, unsigned int *bd_size,
				    unsigned int bd_num)
{
	unsigned int size;
	int i;

	size = skb_headlen(skb);
	while (size > HNS3_MAX_BD_SIZE) {
		bd_size[bd_num++] = HNS3_MAX_BD_SIZE;
		size -= HNS3_MAX_BD_SIZE;

		if (bd_num > HNS3_MAX_TSO_BD_NUM)
			return bd_num;
	}

	if (size) {
		bd_size[bd_num++] = size;
		if (bd_num > HNS3_MAX_TSO_BD_NUM)
			return bd_num;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		size = skb_frag_size(frag);
		if (!size)
			continue;

		while (size > HNS3_MAX_BD_SIZE) {
			bd_size[bd_num++] = HNS3_MAX_BD_SIZE;
			size -= HNS3_MAX_BD_SIZE;

			if (bd_num > HNS3_MAX_TSO_BD_NUM)
				return bd_num;
		}

		bd_size[bd_num++] = size;
		if (bd_num > HNS3_MAX_TSO_BD_NUM)
			return bd_num;
	}

	return bd_num;
}

static unsigned int hns3_tx_bd_num(struct sk_buff *skb, unsigned int *bd_size,
				   u8 max_non_tso_bd_num, unsigned int bd_num,
				   unsigned int recursion_level)
{
#define HNS3_MAX_RECURSION_LEVEL	24

	struct sk_buff *frag_skb;

	/* If the total len is within the max bd limit */
	if (likely(skb->len <= HNS3_MAX_BD_SIZE && !recursion_level &&
		   !skb_has_frag_list(skb) &&
		   skb_shinfo(skb)->nr_frags < max_non_tso_bd_num))
		return skb_shinfo(skb)->nr_frags + 1U;

	if (unlikely(recursion_level >= HNS3_MAX_RECURSION_LEVEL))
		return UINT_MAX;

	bd_num = hns3_skb_bd_num(skb, bd_size, bd_num);
	if (!skb_has_frag_list(skb) || bd_num > HNS3_MAX_TSO_BD_NUM)
		return bd_num;

	skb_walk_frags(skb, frag_skb) {
		bd_num = hns3_tx_bd_num(frag_skb, bd_size, max_non_tso_bd_num,
					bd_num, recursion_level + 1);
		if (bd_num > HNS3_MAX_TSO_BD_NUM)
			return bd_num;
	}

	return bd_num;
}

static unsigned int hns3_gso_hdr_len(struct sk_buff *skb)
{
	if (!skb->encapsulation)
		return skb_transport_offset(skb) + tcp_hdrlen(skb);

	return skb_inner_transport_offset(skb) + inner_tcp_hdrlen(skb);
}

/* HW need every continuous max_non_tso_bd_num buffer data to be larger
 * than MSS, we simplify it by ensuring skb_headlen + the first continuous
 * max_non_tso_bd_num - 1 frags to be larger than gso header len + mss,
 * and the remaining continuous max_non_tso_bd_num - 1 frags to be larger
 * than MSS except the last max_non_tso_bd_num - 1 frags.
 */
static bool hns3_skb_need_linearized(struct sk_buff *skb, unsigned int *bd_size,
				     unsigned int bd_num, u8 max_non_tso_bd_num)
{
	unsigned int tot_len = 0;
	int i;

	for (i = 0; i < max_non_tso_bd_num - 1U; i++)
		tot_len += bd_size[i];

	/* ensure the first max_non_tso_bd_num frags is greater than
	 * mss + header
	 */
	if (tot_len + bd_size[max_non_tso_bd_num - 1U] <
	    skb_shinfo(skb)->gso_size + hns3_gso_hdr_len(skb))
		return true;

	/* ensure every continuous max_non_tso_bd_num - 1 buffer is greater
	 * than mss except the last one.
	 */
	for (i = 0; i < bd_num - max_non_tso_bd_num; i++) {
		tot_len -= bd_size[i];
		tot_len += bd_size[i + max_non_tso_bd_num - 1U];

		if (tot_len < skb_shinfo(skb)->gso_size)
			return true;
	}

	return false;
}

void hns3_shinfo_pack(struct skb_shared_info *shinfo, __u32 *size)
{
	int i;

	for (i = 0; i < MAX_SKB_FRAGS; i++)
		size[i] = skb_frag_size(&shinfo->frags[i]);
}

static int hns3_skb_linearize(struct hns3_enet_ring *ring,
			      struct sk_buff *skb,
			      unsigned int bd_num)
{
	/* 'bd_num == UINT_MAX' means the skb' fraglist has a
	 * recursion level of over HNS3_MAX_RECURSION_LEVEL.
	 */
	if (bd_num == UINT_MAX) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.over_max_recursion++;
		u64_stats_update_end(&ring->syncp);
		return -ENOMEM;
	}

	/* The skb->len has exceeded the hw limitation, linearization
	 * will not help.
	 */
	if (skb->len > HNS3_MAX_TSO_SIZE ||
	    (!skb_is_gso(skb) && skb->len > HNS3_MAX_NON_TSO_SIZE)) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.hw_limitation++;
		u64_stats_update_end(&ring->syncp);
		return -ENOMEM;
	}

	if (__skb_linearize(skb)) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.sw_err_cnt++;
		u64_stats_update_end(&ring->syncp);
		return -ENOMEM;
	}

	return 0;
}

static int hns3_nic_maybe_stop_tx(struct hns3_enet_ring *ring,
				  struct net_device *netdev,
				  struct sk_buff *skb)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	u8 max_non_tso_bd_num = priv->max_non_tso_bd_num;
	unsigned int bd_size[HNS3_MAX_TSO_BD_NUM + 1U];
	unsigned int bd_num;

	bd_num = hns3_tx_bd_num(skb, bd_size, max_non_tso_bd_num, 0, 0);
	if (unlikely(bd_num > max_non_tso_bd_num)) {
		if (bd_num <= HNS3_MAX_TSO_BD_NUM && skb_is_gso(skb) &&
		    !hns3_skb_need_linearized(skb, bd_size, bd_num,
					      max_non_tso_bd_num)) {
			trace_hns3_over_max_bd(skb);
			goto out;
		}

		if (hns3_skb_linearize(ring, skb, bd_num))
			return -ENOMEM;

		bd_num = hns3_tx_bd_count(skb->len);

		u64_stats_update_begin(&ring->syncp);
		ring->stats.tx_copy++;
		u64_stats_update_end(&ring->syncp);
	}

out:
	if (likely(ring_space(ring) >= bd_num))
		return bd_num;

	netif_stop_subqueue(netdev, ring->queue_index);
	smp_mb(); /* Memory barrier before checking ring_space */

	/* Start queue in case hns3_clean_tx_ring has just made room
	 * available and has not seen the queue stopped state performed
	 * by netif_stop_subqueue above.
	 */
	if (ring_space(ring) >= bd_num && netif_carrier_ok(netdev) &&
	    !test_bit(HNS3_NIC_STATE_DOWN, &priv->state)) {
		netif_start_subqueue(netdev, ring->queue_index);
		return bd_num;
	}

	u64_stats_update_begin(&ring->syncp);
	ring->stats.tx_busy++;
	u64_stats_update_end(&ring->syncp);

	return -EBUSY;
}

static void hns3_clear_desc(struct hns3_enet_ring *ring, int next_to_use_orig)
{
	struct device *dev = ring_to_dev(ring);
	unsigned int i;

	for (i = 0; i < ring->desc_num; i++) {
		struct hns3_desc *desc = &ring->desc[ring->next_to_use];
		struct hns3_desc_cb *desc_cb;

		memset(desc, 0, sizeof(*desc));

		/* check if this is where we started */
		if (ring->next_to_use == next_to_use_orig)
			break;

		/* rollback one */
		ring_ptr_move_bw(ring, next_to_use);

		desc_cb = &ring->desc_cb[ring->next_to_use];

		if (!desc_cb->dma)
			continue;

		/* unmap the descriptor dma address */
		if (desc_cb->type & (DESC_TYPE_SKB | DESC_TYPE_FRAGLIST_SKB))
			dma_unmap_single(dev, desc_cb->dma, desc_cb->length,
					 DMA_TO_DEVICE);
		else if (desc_cb->type &
			 (DESC_TYPE_BOUNCE_HEAD | DESC_TYPE_BOUNCE_ALL))
			hns3_tx_spare_rollback(ring, desc_cb->length);
		else if (desc_cb->length)
			dma_unmap_page(dev, desc_cb->dma, desc_cb->length,
				       DMA_TO_DEVICE);

		desc_cb->length = 0;
		desc_cb->dma = 0;
		desc_cb->type = DESC_TYPE_UNKNOWN;
	}
}

static int hns3_fill_skb_to_desc(struct hns3_enet_ring *ring,
				 struct sk_buff *skb, unsigned int type)
{
	struct sk_buff *frag_skb;
	int i, ret, bd_num = 0;

	ret = hns3_map_and_fill_desc(ring, skb, type);
	if (unlikely(ret < 0))
		return ret;

	bd_num += ret;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		ret = hns3_map_and_fill_desc(ring, frag, DESC_TYPE_PAGE);
		if (unlikely(ret < 0))
			return ret;

		bd_num += ret;
	}

	skb_walk_frags(skb, frag_skb) {
		ret = hns3_fill_skb_to_desc(ring, frag_skb,
					    DESC_TYPE_FRAGLIST_SKB);
		if (unlikely(ret < 0))
			return ret;

		bd_num += ret;
	}

	return bd_num;
}

static void hns3_tx_doorbell(struct hns3_enet_ring *ring, int num,
			     bool doorbell)
{
	ring->pending_buf += num;

	if (!doorbell) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.tx_more++;
		u64_stats_update_end(&ring->syncp);
		return;
	}

	if (!ring->pending_buf)
		return;

	writel(ring->pending_buf,
	       ring->tqp->io_base + HNS3_RING_TX_RING_TAIL_REG);
	ring->pending_buf = 0;
	WRITE_ONCE(ring->last_to_use, ring->next_to_use);
}

static void hns3_tsyn(struct net_device *netdev, struct sk_buff *skb,
		      struct hns3_desc *desc)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!(h->ae_algo->ops->set_tx_hwts_info &&
	      h->ae_algo->ops->set_tx_hwts_info(h, skb)))
		return;

	desc->tx.bdtp_fe_sc_vld_ra_ri |= cpu_to_le16(BIT(HNS3_TXD_TSYN_B));
}

static int hns3_handle_tx_bounce(struct hns3_enet_ring *ring,
				 struct sk_buff *skb)
{
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	unsigned int type = DESC_TYPE_BOUNCE_HEAD;
	unsigned int size = skb_headlen(skb);
	dma_addr_t dma;
	int bd_num = 0;
	u32 cb_len;
	void *buf;
	int ret;

	if (skb->len <= ring->tx_copybreak) {
		size = skb->len;
		type = DESC_TYPE_BOUNCE_ALL;
	}

	/* hns3_can_use_tx_bounce() is called to ensure the below
	 * function can always return the tx buffer.
	 */
	buf = hns3_tx_spare_alloc(ring, size, &dma, &cb_len);

	ret = skb_copy_bits(skb, 0, buf, size);
	if (unlikely(ret < 0)) {
		hns3_tx_spare_rollback(ring, cb_len);
		u64_stats_update_begin(&ring->syncp);
		ring->stats.copy_bits_err++;
		u64_stats_update_end(&ring->syncp);
		return ret;
	}

	desc_cb->priv = skb;
	desc_cb->length = cb_len;
	desc_cb->dma = dma;
	desc_cb->type = type;

	bd_num += hns3_fill_desc(ring, dma, size);

	if (type == DESC_TYPE_BOUNCE_HEAD) {
		ret = hns3_fill_skb_to_desc(ring, skb,
					    DESC_TYPE_BOUNCE_HEAD);
		if (unlikely(ret < 0))
			return ret;

		bd_num += ret;
	}

	dma_sync_single_for_device(ring_to_dev(ring), dma, size,
				   DMA_TO_DEVICE);

	u64_stats_update_begin(&ring->syncp);
	ring->stats.tx_bounce++;
	u64_stats_update_end(&ring->syncp);
	return bd_num;
}

static int hns3_handle_tx_sgl(struct hns3_enet_ring *ring,
			      struct sk_buff *skb)
{
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	u32 nfrag = skb_shinfo(skb)->nr_frags + 1;
	struct sg_table *sgt;
	int i, bd_num = 0;
	dma_addr_t dma;
	u32 cb_len;
	int nents;

	if (skb_has_frag_list(skb))
		nfrag = HNS3_MAX_TSO_BD_NUM;

	/* hns3_can_use_tx_sgl() is called to ensure the below
	 * function can always return the tx buffer.
	 */
	sgt = hns3_tx_spare_alloc(ring, HNS3_SGL_SIZE(nfrag),
				  &dma, &cb_len);

	/* scatterlist follows by the sg table */
	sgt->sgl = (struct scatterlist *)(sgt + 1);
	sg_init_table(sgt->sgl, nfrag);
	nents = skb_to_sgvec(skb, sgt->sgl, 0, skb->len);
	if (unlikely(nents < 0)) {
		hns3_tx_spare_rollback(ring, cb_len);
		u64_stats_update_begin(&ring->syncp);
		ring->stats.skb2sgl_err++;
		u64_stats_update_end(&ring->syncp);
		return -ENOMEM;
	}

	sgt->orig_nents = nents;
	sgt->nents = dma_map_sg(ring_to_dev(ring), sgt->sgl, sgt->orig_nents,
				DMA_TO_DEVICE);
	if (unlikely(!sgt->nents)) {
		hns3_tx_spare_rollback(ring, cb_len);
		u64_stats_update_begin(&ring->syncp);
		ring->stats.map_sg_err++;
		u64_stats_update_end(&ring->syncp);
		return -ENOMEM;
	}

	desc_cb->priv = skb;
	desc_cb->length = cb_len;
	desc_cb->dma = dma;
	desc_cb->type = DESC_TYPE_SGL_SKB;

	for (i = 0; i < sgt->nents; i++)
		bd_num += hns3_fill_desc(ring, sg_dma_address(sgt->sgl + i),
					 sg_dma_len(sgt->sgl + i));

	u64_stats_update_begin(&ring->syncp);
	ring->stats.tx_sgl++;
	u64_stats_update_end(&ring->syncp);

	return bd_num;
}

static int hns3_handle_desc_filling(struct hns3_enet_ring *ring,
				    struct sk_buff *skb)
{
	u32 space;

	if (!ring->tx_spare)
		goto out;

	space = hns3_tx_spare_space(ring);

	if (hns3_can_use_tx_sgl(ring, skb, space))
		return hns3_handle_tx_sgl(ring, skb);

	if (hns3_can_use_tx_bounce(ring, skb, space))
		return hns3_handle_tx_bounce(ring, skb);

out:
	return hns3_fill_skb_to_desc(ring, skb, DESC_TYPE_SKB);
}

netdev_tx_t hns3_nic_net_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hns3_enet_ring *ring = &priv->ring[skb->queue_mapping];
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct netdev_queue *dev_queue;
	int pre_ntu, next_to_use_head;
	bool doorbell;
	int ret;

	/* Hardware can only handle short frames above 32 bytes */
	if (skb_put_padto(skb, HNS3_MIN_TX_LEN)) {
		hns3_tx_doorbell(ring, 0, !netdev_xmit_more());

		u64_stats_update_begin(&ring->syncp);
		ring->stats.sw_err_cnt++;
		u64_stats_update_end(&ring->syncp);

		return NETDEV_TX_OK;
	}

	/* Prefetch the data used later */
	prefetch(skb->data);

	ret = hns3_nic_maybe_stop_tx(ring, netdev, skb);
	if (unlikely(ret <= 0)) {
		if (ret == -EBUSY) {
			hns3_tx_doorbell(ring, 0, true);
			return NETDEV_TX_BUSY;
		}

		hns3_rl_err(netdev, "xmit error: %d!\n", ret);
		goto out_err_tx_ok;
	}

	next_to_use_head = ring->next_to_use;

	ret = hns3_fill_skb_desc(ring, skb, &ring->desc[ring->next_to_use],
				 desc_cb);
	if (unlikely(ret < 0))
		goto fill_err;

	/* 'ret < 0' means filling error, 'ret == 0' means skb->len is
	 * zero, which is unlikely, and 'ret > 0' means how many tx desc
	 * need to be notified to the hw.
	 */
	ret = hns3_handle_desc_filling(ring, skb);
	if (unlikely(ret <= 0))
		goto fill_err;

	pre_ntu = ring->next_to_use ? (ring->next_to_use - 1) :
					(ring->desc_num - 1);

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		hns3_tsyn(netdev, skb, &ring->desc[pre_ntu]);

	ring->desc[pre_ntu].tx.bdtp_fe_sc_vld_ra_ri |=
				cpu_to_le16(BIT(HNS3_TXD_FE_B));
	trace_hns3_tx_desc(ring, pre_ntu);

	skb_tx_timestamp(skb);

	/* Complete translate all packets */
	dev_queue = netdev_get_tx_queue(netdev, ring->queue_index);
	doorbell = __netdev_tx_sent_queue(dev_queue, desc_cb->send_bytes,
					  netdev_xmit_more());
	hns3_tx_doorbell(ring, ret, doorbell);

	return NETDEV_TX_OK;

fill_err:
	hns3_clear_desc(ring, next_to_use_head);

out_err_tx_ok:
	dev_kfree_skb_any(skb);
	hns3_tx_doorbell(ring, 0, !netdev_xmit_more());
	return NETDEV_TX_OK;
}

static int hns3_nic_net_set_mac_address(struct net_device *netdev, void *p)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct sockaddr *mac_addr = p;
	int ret;

	if (!mac_addr || !is_valid_ether_addr((const u8 *)mac_addr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, mac_addr->sa_data)) {
		netdev_info(netdev, "already using mac address %pM\n",
			    mac_addr->sa_data);
		return 0;
	}

	/* For VF device, if there is a perm_addr, then the user will not
	 * be allowed to change the address.
	 */
	if (!hns3_is_phys_func(h->pdev) &&
	    !is_zero_ether_addr(netdev->perm_addr)) {
		netdev_err(netdev, "has permanent MAC %pM, user MAC %pM not allow\n",
			   netdev->perm_addr, mac_addr->sa_data);
		return -EPERM;
	}

	ret = h->ae_algo->ops->set_mac_addr(h, mac_addr->sa_data, false);
	if (ret) {
		netdev_err(netdev, "set_mac_address fail, ret=%d!\n", ret);
		return ret;
	}

	eth_hw_addr_set(netdev, mac_addr->sa_data);

	return 0;
}

static int hns3_nic_do_ioctl(struct net_device *netdev,
			     struct ifreq *ifr, int cmd)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!netif_running(netdev))
		return -EINVAL;

	if (!h->ae_algo->ops->do_ioctl)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->do_ioctl(h, ifr, cmd);
}

static int hns3_nic_set_features(struct net_device *netdev,
				 netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	bool enable;
	int ret;

	if (changed & (NETIF_F_GRO_HW) && h->ae_algo->ops->set_gro_en) {
		enable = !!(features & NETIF_F_GRO_HW);
		ret = h->ae_algo->ops->set_gro_en(h, enable);
		if (ret)
			return ret;
	}

	if ((changed & NETIF_F_HW_VLAN_CTAG_RX) &&
	    h->ae_algo->ops->enable_hw_strip_rxvtag) {
		enable = !!(features & NETIF_F_HW_VLAN_CTAG_RX);
		ret = h->ae_algo->ops->enable_hw_strip_rxvtag(h, enable);
		if (ret)
			return ret;
	}

	if ((changed & NETIF_F_NTUPLE) && h->ae_algo->ops->enable_fd) {
		enable = !!(features & NETIF_F_NTUPLE);
		h->ae_algo->ops->enable_fd(h, enable);
	}

	if ((netdev->features & NETIF_F_HW_TC) > (features & NETIF_F_HW_TC) &&
	    h->ae_algo->ops->cls_flower_active(h)) {
		netdev_err(netdev,
			   "there are offloaded TC filters active, cannot disable HW TC offload");
		return -EINVAL;
	}

	if ((changed & NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    h->ae_algo->ops->enable_vlan_filter) {
		enable = !!(features & NETIF_F_HW_VLAN_CTAG_FILTER);
		ret = h->ae_algo->ops->enable_vlan_filter(h, enable);
		if (ret)
			return ret;
	}

	netdev->features = features;
	return 0;
}

static netdev_features_t hns3_features_check(struct sk_buff *skb,
					     struct net_device *dev,
					     netdev_features_t features)
{
#define HNS3_MAX_HDR_LEN	480U
#define HNS3_MAX_L4_HDR_LEN	60U

	size_t len;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return features;

	if (skb->encapsulation)
		len = skb_inner_transport_header(skb) - skb->data;
	else
		len = skb_transport_header(skb) - skb->data;

	/* Assume L4 is 60 byte as TCP is the only protocol with a
	 * a flexible value, and it's max len is 60 bytes.
	 */
	len += HNS3_MAX_L4_HDR_LEN;

	/* Hardware only supports checksum on the skb with a max header
	 * len of 480 bytes.
	 */
	if (len > HNS3_MAX_HDR_LEN)
		features &= ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);

	return features;
}

static void hns3_nic_get_stats64(struct net_device *netdev,
				 struct rtnl_link_stats64 *stats)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int queue_num = priv->ae_handle->kinfo.num_tqps;
	struct hnae3_handle *handle = priv->ae_handle;
	struct hns3_enet_ring *ring;
	u64 rx_length_errors = 0;
	u64 rx_crc_errors = 0;
	u64 rx_multicast = 0;
	unsigned int start;
	u64 tx_errors = 0;
	u64 rx_errors = 0;
	unsigned int idx;
	u64 tx_bytes = 0;
	u64 rx_bytes = 0;
	u64 tx_pkts = 0;
	u64 rx_pkts = 0;
	u64 tx_drop = 0;
	u64 rx_drop = 0;

	if (test_bit(HNS3_NIC_STATE_DOWN, &priv->state))
		return;

	handle->ae_algo->ops->update_stats(handle, &netdev->stats);

	for (idx = 0; idx < queue_num; idx++) {
		/* fetch the tx stats */
		ring = &priv->ring[idx];
		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			tx_bytes += ring->stats.tx_bytes;
			tx_pkts += ring->stats.tx_pkts;
			tx_drop += ring->stats.sw_err_cnt;
			tx_drop += ring->stats.tx_vlan_err;
			tx_drop += ring->stats.tx_l4_proto_err;
			tx_drop += ring->stats.tx_l2l3l4_err;
			tx_drop += ring->stats.tx_tso_err;
			tx_drop += ring->stats.over_max_recursion;
			tx_drop += ring->stats.hw_limitation;
			tx_drop += ring->stats.copy_bits_err;
			tx_drop += ring->stats.skb2sgl_err;
			tx_drop += ring->stats.map_sg_err;
			tx_errors += ring->stats.sw_err_cnt;
			tx_errors += ring->stats.tx_vlan_err;
			tx_errors += ring->stats.tx_l4_proto_err;
			tx_errors += ring->stats.tx_l2l3l4_err;
			tx_errors += ring->stats.tx_tso_err;
			tx_errors += ring->stats.over_max_recursion;
			tx_errors += ring->stats.hw_limitation;
			tx_errors += ring->stats.copy_bits_err;
			tx_errors += ring->stats.skb2sgl_err;
			tx_errors += ring->stats.map_sg_err;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

		/* fetch the rx stats */
		ring = &priv->ring[idx + queue_num];
		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			rx_bytes += ring->stats.rx_bytes;
			rx_pkts += ring->stats.rx_pkts;
			rx_drop += ring->stats.l2_err;
			rx_errors += ring->stats.l2_err;
			rx_errors += ring->stats.l3l4_csum_err;
			rx_crc_errors += ring->stats.l2_err;
			rx_multicast += ring->stats.rx_multicast;
			rx_length_errors += ring->stats.err_pkt_len;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));
	}

	stats->tx_bytes = tx_bytes;
	stats->tx_packets = tx_pkts;
	stats->rx_bytes = rx_bytes;
	stats->rx_packets = rx_pkts;

	stats->rx_errors = rx_errors;
	stats->multicast = rx_multicast;
	stats->rx_length_errors = rx_length_errors;
	stats->rx_crc_errors = rx_crc_errors;
	stats->rx_missed_errors = netdev->stats.rx_missed_errors;

	stats->tx_errors = tx_errors;
	stats->rx_dropped = rx_drop;
	stats->tx_dropped = tx_drop;
	stats->collisions = netdev->stats.collisions;
	stats->rx_over_errors = netdev->stats.rx_over_errors;
	stats->rx_frame_errors = netdev->stats.rx_frame_errors;
	stats->rx_fifo_errors = netdev->stats.rx_fifo_errors;
	stats->tx_aborted_errors = netdev->stats.tx_aborted_errors;
	stats->tx_carrier_errors = netdev->stats.tx_carrier_errors;
	stats->tx_fifo_errors = netdev->stats.tx_fifo_errors;
	stats->tx_heartbeat_errors = netdev->stats.tx_heartbeat_errors;
	stats->tx_window_errors = netdev->stats.tx_window_errors;
	stats->rx_compressed = netdev->stats.rx_compressed;
	stats->tx_compressed = netdev->stats.tx_compressed;
}

static int hns3_setup_tc(struct net_device *netdev, void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio_qopt = type_data;
	struct hnae3_knic_private_info *kinfo;
	u8 tc = mqprio_qopt->qopt.num_tc;
	u16 mode = mqprio_qopt->mode;
	u8 hw = mqprio_qopt->qopt.hw;
	struct hnae3_handle *h;

	if (!((hw == TC_MQPRIO_HW_OFFLOAD_TCS &&
	       mode == TC_MQPRIO_MODE_CHANNEL) || (!hw && tc == 0)))
		return -EOPNOTSUPP;

	if (tc > HNAE3_MAX_TC)
		return -EINVAL;

	if (!netdev)
		return -EINVAL;

	h = hns3_get_handle(netdev);
	kinfo = &h->kinfo;

	netif_dbg(h, drv, netdev, "setup tc: num_tc=%u\n", tc);

	return (kinfo->dcb_ops && kinfo->dcb_ops->setup_tc) ?
		kinfo->dcb_ops->setup_tc(h, mqprio_qopt) : -EOPNOTSUPP;
}

static int hns3_setup_tc_cls_flower(struct hns3_nic_priv *priv,
				    struct flow_cls_offload *flow)
{
	int tc = tc_classid_to_hwtc(priv->netdev, flow->classid);
	struct hnae3_handle *h = hns3_get_handle(priv->netdev);

	switch (flow->command) {
	case FLOW_CLS_REPLACE:
		if (h->ae_algo->ops->add_cls_flower)
			return h->ae_algo->ops->add_cls_flower(h, flow, tc);
		break;
	case FLOW_CLS_DESTROY:
		if (h->ae_algo->ops->del_cls_flower)
			return h->ae_algo->ops->del_cls_flower(h, flow);
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int hns3_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				  void *cb_priv)
{
	struct hns3_nic_priv *priv = cb_priv;

	if (!tc_cls_can_offload_and_chain0(priv->netdev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return hns3_setup_tc_cls_flower(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(hns3_block_cb_list);

static int hns3_nic_setup_tc(struct net_device *dev, enum tc_setup_type type,
			     void *type_data)
{
	struct hns3_nic_priv *priv = netdev_priv(dev);
	int ret;

	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		ret = hns3_setup_tc(dev, type_data);
		break;
	case TC_SETUP_BLOCK:
		ret = flow_block_cb_setup_simple(type_data,
						 &hns3_block_cb_list,
						 hns3_setup_tc_block_cb,
						 priv, priv, true);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int hns3_vlan_rx_add_vid(struct net_device *netdev,
				__be16 proto, u16 vid)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vlan_filter)
		ret = h->ae_algo->ops->set_vlan_filter(h, proto, vid, false);

	return ret;
}

static int hns3_vlan_rx_kill_vid(struct net_device *netdev,
				 __be16 proto, u16 vid)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = -EIO;

	if (h->ae_algo->ops->set_vlan_filter)
		ret = h->ae_algo->ops->set_vlan_filter(h, proto, vid, true);

	return ret;
}

static int hns3_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan,
				u8 qos, __be16 vlan_proto)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = -EIO;

	netif_dbg(h, drv, netdev,
		  "set vf vlan: vf=%d, vlan=%u, qos=%u, vlan_proto=0x%x\n",
		  vf, vlan, qos, ntohs(vlan_proto));

	if (h->ae_algo->ops->set_vf_vlan_filter)
		ret = h->ae_algo->ops->set_vf_vlan_filter(h, vf, vlan,
							  qos, vlan_proto);

	return ret;
}

static int hns3_set_vf_spoofchk(struct net_device *netdev, int vf, bool enable)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	if (!handle->ae_algo->ops->set_vf_spoofchk)
		return -EOPNOTSUPP;

	return handle->ae_algo->ops->set_vf_spoofchk(handle, vf, enable);
}

static int hns3_set_vf_trust(struct net_device *netdev, int vf, bool enable)
{
	struct hnae3_handle *handle = hns3_get_handle(netdev);

	if (!handle->ae_algo->ops->set_vf_trust)
		return -EOPNOTSUPP;

	return handle->ae_algo->ops->set_vf_trust(handle, vf, enable);
}

static int hns3_nic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret;

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	if (!h->ae_algo->ops->set_mtu)
		return -EOPNOTSUPP;

	netif_dbg(h, drv, netdev,
		  "change mtu from %u to %d\n", netdev->mtu, new_mtu);

	ret = h->ae_algo->ops->set_mtu(h, new_mtu);
	if (ret)
		netdev_err(netdev, "failed to change MTU in hardware %d\n",
			   ret);
	else
		netdev->mtu = new_mtu;

	return ret;
}

static bool hns3_get_tx_timeo_queue_info(struct net_device *ndev)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = hns3_get_handle(ndev);
	struct hns3_enet_ring *tx_ring;
	struct napi_struct *napi;
	int timeout_queue = 0;
	int hw_head, hw_tail;
	int fbd_num, fbd_oft;
	int ebd_num, ebd_oft;
	int bd_num, bd_err;
	int ring_en, tc;
	int i;

	/* Find the stopped queue the same way the stack does */
	for (i = 0; i < ndev->num_tx_queues; i++) {
		struct netdev_queue *q;
		unsigned long trans_start;

		q = netdev_get_tx_queue(ndev, i);
		trans_start = q->trans_start;
		if (netif_xmit_stopped(q) &&
		    time_after(jiffies,
			       (trans_start + ndev->watchdog_timeo))) {
			timeout_queue = i;
			netdev_info(ndev, "queue state: 0x%lx, delta msecs: %u\n",
				    q->state,
				    jiffies_to_msecs(jiffies - trans_start));
			break;
		}
	}

	if (i == ndev->num_tx_queues) {
		netdev_info(ndev,
			    "no netdev TX timeout queue found, timeout count: %llu\n",
			    priv->tx_timeout_count);
		return false;
	}

	priv->tx_timeout_count++;

	tx_ring = &priv->ring[timeout_queue];
	napi = &tx_ring->tqp_vector->napi;

	netdev_info(ndev,
		    "tx_timeout count: %llu, queue id: %d, SW_NTU: 0x%x, SW_NTC: 0x%x, napi state: %lu\n",
		    priv->tx_timeout_count, timeout_queue, tx_ring->next_to_use,
		    tx_ring->next_to_clean, napi->state);

	netdev_info(ndev,
		    "tx_pkts: %llu, tx_bytes: %llu, sw_err_cnt: %llu, tx_pending: %d\n",
		    tx_ring->stats.tx_pkts, tx_ring->stats.tx_bytes,
		    tx_ring->stats.sw_err_cnt, tx_ring->pending_buf);

	netdev_info(ndev,
		    "seg_pkt_cnt: %llu, tx_more: %llu, restart_queue: %llu, tx_busy: %llu\n",
		    tx_ring->stats.seg_pkt_cnt, tx_ring->stats.tx_more,
		    tx_ring->stats.restart_queue, tx_ring->stats.tx_busy);

	/* When mac received many pause frames continuous, it's unable to send
	 * packets, which may cause tx timeout
	 */
	if (h->ae_algo->ops->get_mac_stats) {
		struct hns3_mac_stats mac_stats;

		h->ae_algo->ops->get_mac_stats(h, &mac_stats);
		netdev_info(ndev, "tx_pause_cnt: %llu, rx_pause_cnt: %llu\n",
			    mac_stats.tx_pause_cnt, mac_stats.rx_pause_cnt);
	}

	hw_head = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_HEAD_REG);
	hw_tail = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_TAIL_REG);
	fbd_num = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_FBDNUM_REG);
	fbd_oft = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_OFFSET_REG);
	ebd_num = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_EBDNUM_REG);
	ebd_oft = readl_relaxed(tx_ring->tqp->io_base +
				HNS3_RING_TX_RING_EBD_OFFSET_REG);
	bd_num = readl_relaxed(tx_ring->tqp->io_base +
			       HNS3_RING_TX_RING_BD_NUM_REG);
	bd_err = readl_relaxed(tx_ring->tqp->io_base +
			       HNS3_RING_TX_RING_BD_ERR_REG);
	ring_en = readl_relaxed(tx_ring->tqp->io_base + HNS3_RING_EN_REG);
	tc = readl_relaxed(tx_ring->tqp->io_base + HNS3_RING_TX_RING_TC_REG);

	netdev_info(ndev,
		    "BD_NUM: 0x%x HW_HEAD: 0x%x, HW_TAIL: 0x%x, BD_ERR: 0x%x, INT: 0x%x\n",
		    bd_num, hw_head, hw_tail, bd_err,
		    readl(tx_ring->tqp_vector->mask_addr));
	netdev_info(ndev,
		    "RING_EN: 0x%x, TC: 0x%x, FBD_NUM: 0x%x FBD_OFT: 0x%x, EBD_NUM: 0x%x, EBD_OFT: 0x%x\n",
		    ring_en, tc, fbd_num, fbd_oft, ebd_num, ebd_oft);

	return true;
}

static void hns3_nic_net_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hnae3_handle *h = priv->ae_handle;

	if (!hns3_get_tx_timeo_queue_info(ndev))
		return;

	/* request the reset, and let the hclge to determine
	 * which reset level should be done
	 */
	if (h->ae_algo->ops->reset_event)
		h->ae_algo->ops->reset_event(h->pdev, h);
}

#ifdef CONFIG_RFS_ACCEL
static int hns3_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			      u16 rxq_index, u32 flow_id)
{
	struct hnae3_handle *h = hns3_get_handle(dev);
	struct flow_keys fkeys;

	if (!h->ae_algo->ops->add_arfs_entry)
		return -EOPNOTSUPP;

	if (skb->encapsulation)
		return -EPROTONOSUPPORT;

	if (!skb_flow_dissect_flow_keys(skb, &fkeys, 0))
		return -EPROTONOSUPPORT;

	if ((fkeys.basic.n_proto != htons(ETH_P_IP) &&
	     fkeys.basic.n_proto != htons(ETH_P_IPV6)) ||
	    (fkeys.basic.ip_proto != IPPROTO_TCP &&
	     fkeys.basic.ip_proto != IPPROTO_UDP))
		return -EPROTONOSUPPORT;

	return h->ae_algo->ops->add_arfs_entry(h, rxq_index, flow_id, &fkeys);
}
#endif

static int hns3_nic_get_vf_config(struct net_device *ndev, int vf,
				  struct ifla_vf_info *ivf)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (!h->ae_algo->ops->get_vf_config)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->get_vf_config(h, vf, ivf);
}

static int hns3_nic_set_vf_link_state(struct net_device *ndev, int vf,
				      int link_state)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (!h->ae_algo->ops->set_vf_link_state)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->set_vf_link_state(h, vf, link_state);
}

static int hns3_nic_set_vf_rate(struct net_device *ndev, int vf,
				int min_tx_rate, int max_tx_rate)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (!h->ae_algo->ops->set_vf_rate)
		return -EOPNOTSUPP;

	return h->ae_algo->ops->set_vf_rate(h, vf, min_tx_rate, max_tx_rate,
					    false);
}

static int hns3_nic_set_vf_mac(struct net_device *netdev, int vf_id, u8 *mac)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (!h->ae_algo->ops->set_vf_mac)
		return -EOPNOTSUPP;

	if (is_multicast_ether_addr(mac)) {
		netdev_err(netdev,
			   "Invalid MAC:%pM specified. Could not set MAC\n",
			   mac);
		return -EINVAL;
	}

	return h->ae_algo->ops->set_vf_mac(h, vf_id, mac);
}

static const struct net_device_ops hns3_nic_netdev_ops = {
	.ndo_open		= hns3_nic_net_open,
	.ndo_stop		= hns3_nic_net_stop,
	.ndo_start_xmit		= hns3_nic_net_xmit,
	.ndo_tx_timeout		= hns3_nic_net_timeout,
	.ndo_set_mac_address	= hns3_nic_net_set_mac_address,
	.ndo_eth_ioctl		= hns3_nic_do_ioctl,
	.ndo_change_mtu		= hns3_nic_change_mtu,
	.ndo_set_features	= hns3_nic_set_features,
	.ndo_features_check	= hns3_features_check,
	.ndo_get_stats64	= hns3_nic_get_stats64,
	.ndo_setup_tc		= hns3_nic_setup_tc,
	.ndo_set_rx_mode	= hns3_nic_set_rx_mode,
	.ndo_vlan_rx_add_vid	= hns3_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= hns3_vlan_rx_kill_vid,
	.ndo_set_vf_vlan	= hns3_ndo_set_vf_vlan,
	.ndo_set_vf_spoofchk	= hns3_set_vf_spoofchk,
	.ndo_set_vf_trust	= hns3_set_vf_trust,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= hns3_rx_flow_steer,
#endif
	.ndo_get_vf_config	= hns3_nic_get_vf_config,
	.ndo_set_vf_link_state	= hns3_nic_set_vf_link_state,
	.ndo_set_vf_rate	= hns3_nic_set_vf_rate,
	.ndo_set_vf_mac		= hns3_nic_set_vf_mac,
};

bool hns3_is_phys_func(struct pci_dev *pdev)
{
	u32 dev_id = pdev->device;

	switch (dev_id) {
	case HNAE3_DEV_ID_GE:
	case HNAE3_DEV_ID_25GE:
	case HNAE3_DEV_ID_25GE_RDMA:
	case HNAE3_DEV_ID_25GE_RDMA_MACSEC:
	case HNAE3_DEV_ID_50GE_RDMA:
	case HNAE3_DEV_ID_50GE_RDMA_MACSEC:
	case HNAE3_DEV_ID_100G_RDMA_MACSEC:
	case HNAE3_DEV_ID_200G_RDMA:
		return true;
	case HNAE3_DEV_ID_VF:
	case HNAE3_DEV_ID_RDMA_DCB_PFC_VF:
		return false;
	default:
		dev_warn(&pdev->dev, "un-recognized pci device-id %u",
			 dev_id);
	}

	return false;
}

static void hns3_disable_sriov(struct pci_dev *pdev)
{
	/* If our VFs are assigned we cannot shut down SR-IOV
	 * without causing issues, so just leave the hardware
	 * available but disabled
	 */
	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev,
			 "disabling driver while VFs are assigned\n");
		return;
	}

	pci_disable_sriov(pdev);
}

/* hns3_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in hns3_pci_tbl
 *
 * hns3_probe initializes a PF identified by a pci_dev structure.
 * The OS initialization, configuring of the PF private structure,
 * and a hardware reset occur.
 *
 * Returns 0 on success, negative on failure
 */
static int hns3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct hnae3_ae_dev *ae_dev;
	int ret;

	ae_dev = devm_kzalloc(&pdev->dev, sizeof(*ae_dev), GFP_KERNEL);
	if (!ae_dev)
		return -ENOMEM;

	ae_dev->pdev = pdev;
	ae_dev->flag = ent->driver_data;
	pci_set_drvdata(pdev, ae_dev);

	ret = hnae3_register_ae_dev(ae_dev);
	if (ret)
		pci_set_drvdata(pdev, NULL);

	return ret;
}

/* hns3_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void hns3_remove(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	if (hns3_is_phys_func(pdev) && IS_ENABLED(CONFIG_PCI_IOV))
		hns3_disable_sriov(pdev);

	hnae3_unregister_ae_dev(ae_dev);
	pci_set_drvdata(pdev, NULL);
}

/**
 * hns3_pci_sriov_configure
 * @pdev: pointer to a pci_dev structure
 * @num_vfs: number of VFs to allocate
 *
 * Enable or change the number of VFs. Called when the user updates the number
 * of VFs in sysfs.
 **/
static int hns3_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	int ret;

	if (!(hns3_is_phys_func(pdev) && IS_ENABLED(CONFIG_PCI_IOV))) {
		dev_warn(&pdev->dev, "Can not config SRIOV\n");
		return -EINVAL;
	}

	if (num_vfs) {
		ret = pci_enable_sriov(pdev, num_vfs);
		if (ret)
			dev_err(&pdev->dev, "SRIOV enable failed %d\n", ret);
		else
			return num_vfs;
	} else if (!pci_vfs_assigned(pdev)) {
		pci_disable_sriov(pdev);
	} else {
		dev_warn(&pdev->dev,
			 "Unable to free VFs because some are assigned to VMs.\n");
	}

	return 0;
}

static void hns3_shutdown(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	hnae3_unregister_ae_dev(ae_dev);
	pci_set_drvdata(pdev, NULL);

	if (system_state == SYSTEM_POWER_OFF)
		pci_set_power_state(pdev, PCI_D3hot);
}

static int __maybe_unused hns3_suspend(struct device *dev)
{
	struct hnae3_ae_dev *ae_dev = dev_get_drvdata(dev);

	if (ae_dev && hns3_is_phys_func(ae_dev->pdev)) {
		dev_info(dev, "Begin to suspend.\n");
		if (ae_dev->ops && ae_dev->ops->reset_prepare)
			ae_dev->ops->reset_prepare(ae_dev, HNAE3_FUNC_RESET);
	}

	return 0;
}

static int __maybe_unused hns3_resume(struct device *dev)
{
	struct hnae3_ae_dev *ae_dev = dev_get_drvdata(dev);

	if (ae_dev && hns3_is_phys_func(ae_dev->pdev)) {
		dev_info(dev, "Begin to resume.\n");
		if (ae_dev->ops && ae_dev->ops->reset_done)
			ae_dev->ops->reset_done(ae_dev);
	}

	return 0;
}

static pci_ers_result_t hns3_error_detected(struct pci_dev *pdev,
					    pci_channel_state_t state)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
	pci_ers_result_t ret;

	dev_info(&pdev->dev, "PCI error detected, state(=%u)!!\n", state);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (!ae_dev || !ae_dev->ops) {
		dev_err(&pdev->dev,
			"Can't recover - error happened before device initialized\n");
		return PCI_ERS_RESULT_NONE;
	}

	if (ae_dev->ops->handle_hw_ras_error)
		ret = ae_dev->ops->handle_hw_ras_error(ae_dev);
	else
		return PCI_ERS_RESULT_NONE;

	return ret;
}

static pci_ers_result_t hns3_slot_reset(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
	const struct hnae3_ae_ops *ops;
	enum hnae3_reset_type reset_type;
	struct device *dev = &pdev->dev;

	if (!ae_dev || !ae_dev->ops)
		return PCI_ERS_RESULT_NONE;

	ops = ae_dev->ops;
	/* request the reset */
	if (ops->reset_event && ops->get_reset_level &&
	    ops->set_default_reset_request) {
		if (ae_dev->hw_err_reset_req) {
			reset_type = ops->get_reset_level(ae_dev,
						&ae_dev->hw_err_reset_req);
			ops->set_default_reset_request(ae_dev, reset_type);
			dev_info(dev, "requesting reset due to PCI error\n");
			ops->reset_event(pdev, NULL);
		}

		return PCI_ERS_RESULT_RECOVERED;
	}

	return PCI_ERS_RESULT_DISCONNECT;
}

static void hns3_reset_prepare(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "FLR prepare\n");
	if (ae_dev && ae_dev->ops && ae_dev->ops->reset_prepare)
		ae_dev->ops->reset_prepare(ae_dev, HNAE3_FLR_RESET);
}

static void hns3_reset_done(struct pci_dev *pdev)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "FLR done\n");
	if (ae_dev && ae_dev->ops && ae_dev->ops->reset_done)
		ae_dev->ops->reset_done(ae_dev);
}

static const struct pci_error_handlers hns3_err_handler = {
	.error_detected = hns3_error_detected,
	.slot_reset     = hns3_slot_reset,
	.reset_prepare	= hns3_reset_prepare,
	.reset_done	= hns3_reset_done,
};

static SIMPLE_DEV_PM_OPS(hns3_pm_ops, hns3_suspend, hns3_resume);

static struct pci_driver hns3_driver = {
	.name     = hns3_driver_name,
	.id_table = hns3_pci_tbl,
	.probe    = hns3_probe,
	.remove   = hns3_remove,
	.shutdown = hns3_shutdown,
	.driver.pm  = &hns3_pm_ops,
	.sriov_configure = hns3_pci_sriov_configure,
	.err_handler    = &hns3_err_handler,
};

/* set default feature to hns3 */
static void hns3_set_default_feature(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct pci_dev *pdev = h->pdev;
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	netdev->priv_flags |= IFF_UNICAST_FLT;

	netdev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;

	netdev->features |= NETIF_F_HW_VLAN_CTAG_FILTER |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_GRE |
		NETIF_F_GSO_GRE_CSUM | NETIF_F_GSO_UDP_TUNNEL |
		NETIF_F_SCTP_CRC | NETIF_F_FRAGLIST;

	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V2) {
		netdev->features |= NETIF_F_GRO_HW;

		if (!(h->flags & HNAE3_SUPPORT_VF))
			netdev->features |= NETIF_F_NTUPLE;
	}

	if (test_bit(HNAE3_DEV_SUPPORT_UDP_GSO_B, ae_dev->caps))
		netdev->features |= NETIF_F_GSO_UDP_L4;

	if (test_bit(HNAE3_DEV_SUPPORT_HW_TX_CSUM_B, ae_dev->caps))
		netdev->features |= NETIF_F_HW_CSUM;
	else
		netdev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;

	if (test_bit(HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B, ae_dev->caps))
		netdev->features |= NETIF_F_GSO_UDP_TUNNEL_CSUM;

	if (test_bit(HNAE3_DEV_SUPPORT_FD_FORWARD_TC_B, ae_dev->caps))
		netdev->features |= NETIF_F_HW_TC;

	netdev->hw_features |= netdev->features;
	if (!test_bit(HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B, ae_dev->caps))
		netdev->hw_features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;

	netdev->vlan_features |= netdev->features &
		~(NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_CTAG_TX |
		  NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_GRO_HW | NETIF_F_NTUPLE |
		  NETIF_F_HW_TC);

	netdev->hw_enc_features |= netdev->vlan_features | NETIF_F_TSO_MANGLEID;
}

static int hns3_alloc_buffer(struct hns3_enet_ring *ring,
			     struct hns3_desc_cb *cb)
{
	unsigned int order = hns3_page_order(ring);
	struct page *p;

	if (ring->page_pool) {
		p = page_pool_dev_alloc_frag(ring->page_pool,
					     &cb->page_offset,
					     hns3_buf_size(ring));
		if (unlikely(!p))
			return -ENOMEM;

		cb->priv = p;
		cb->buf = page_address(p);
		cb->dma = page_pool_get_dma_addr(p);
		cb->type = DESC_TYPE_PP_FRAG;
		cb->reuse_flag = 0;
		return 0;
	}

	p = dev_alloc_pages(order);
	if (!p)
		return -ENOMEM;

	cb->priv = p;
	cb->page_offset = 0;
	cb->reuse_flag = 0;
	cb->buf  = page_address(p);
	cb->length = hns3_page_size(ring);
	cb->type = DESC_TYPE_PAGE;
	page_ref_add(p, USHRT_MAX - 1);
	cb->pagecnt_bias = USHRT_MAX;

	return 0;
}

static void hns3_free_buffer(struct hns3_enet_ring *ring,
			     struct hns3_desc_cb *cb, int budget)
{
	if (cb->type & (DESC_TYPE_SKB | DESC_TYPE_BOUNCE_HEAD |
			DESC_TYPE_BOUNCE_ALL | DESC_TYPE_SGL_SKB))
		napi_consume_skb(cb->priv, budget);
	else if (!HNAE3_IS_TX_RING(ring)) {
		if (cb->type & DESC_TYPE_PAGE && cb->pagecnt_bias)
			__page_frag_cache_drain(cb->priv, cb->pagecnt_bias);
		else if (cb->type & DESC_TYPE_PP_FRAG)
			page_pool_put_full_page(ring->page_pool, cb->priv,
						false);
	}
	memset(cb, 0, sizeof(*cb));
}

static int hns3_map_buffer(struct hns3_enet_ring *ring, struct hns3_desc_cb *cb)
{
	cb->dma = dma_map_page(ring_to_dev(ring), cb->priv, 0,
			       cb->length, ring_to_dma_dir(ring));

	if (unlikely(dma_mapping_error(ring_to_dev(ring), cb->dma)))
		return -EIO;

	return 0;
}

static void hns3_unmap_buffer(struct hns3_enet_ring *ring,
			      struct hns3_desc_cb *cb)
{
	if (cb->type & (DESC_TYPE_SKB | DESC_TYPE_FRAGLIST_SKB))
		dma_unmap_single(ring_to_dev(ring), cb->dma, cb->length,
				 ring_to_dma_dir(ring));
	else if ((cb->type & DESC_TYPE_PAGE) && cb->length)
		dma_unmap_page(ring_to_dev(ring), cb->dma, cb->length,
			       ring_to_dma_dir(ring));
	else if (cb->type & (DESC_TYPE_BOUNCE_ALL | DESC_TYPE_BOUNCE_HEAD |
			     DESC_TYPE_SGL_SKB))
		hns3_tx_spare_reclaim_cb(ring, cb);
}

static void hns3_buffer_detach(struct hns3_enet_ring *ring, int i)
{
	hns3_unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc[i].addr = 0;
	ring->desc_cb[i].refill = 0;
}

static void hns3_free_buffer_detach(struct hns3_enet_ring *ring, int i,
				    int budget)
{
	struct hns3_desc_cb *cb = &ring->desc_cb[i];

	if (!ring->desc_cb[i].dma)
		return;

	hns3_buffer_detach(ring, i);
	hns3_free_buffer(ring, cb, budget);
}

static void hns3_free_buffers(struct hns3_enet_ring *ring)
{
	int i;

	for (i = 0; i < ring->desc_num; i++)
		hns3_free_buffer_detach(ring, i, 0);
}

/* free desc along with its attached buffer */
static void hns3_free_desc(struct hns3_enet_ring *ring)
{
	int size = ring->desc_num * sizeof(ring->desc[0]);

	hns3_free_buffers(ring);

	if (ring->desc) {
		dma_free_coherent(ring_to_dev(ring), size,
				  ring->desc, ring->desc_dma_addr);
		ring->desc = NULL;
	}
}

static int hns3_alloc_desc(struct hns3_enet_ring *ring)
{
	int size = ring->desc_num * sizeof(ring->desc[0]);

	ring->desc = dma_alloc_coherent(ring_to_dev(ring), size,
					&ring->desc_dma_addr, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

static int hns3_alloc_and_map_buffer(struct hns3_enet_ring *ring,
				   struct hns3_desc_cb *cb)
{
	int ret;

	ret = hns3_alloc_buffer(ring, cb);
	if (ret || ring->page_pool)
		goto out;

	ret = hns3_map_buffer(ring, cb);
	if (ret)
		goto out_with_buf;

	return 0;

out_with_buf:
	hns3_free_buffer(ring, cb, 0);
out:
	return ret;
}

static int hns3_alloc_and_attach_buffer(struct hns3_enet_ring *ring, int i)
{
	int ret = hns3_alloc_and_map_buffer(ring, &ring->desc_cb[i]);

	if (ret)
		return ret;

	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma +
					 ring->desc_cb[i].page_offset);
	ring->desc_cb[i].refill = 1;

	return 0;
}

/* Allocate memory for raw pkg, and map with dma */
static int hns3_alloc_ring_buffers(struct hns3_enet_ring *ring)
{
	int i, j, ret;

	for (i = 0; i < ring->desc_num; i++) {
		ret = hns3_alloc_and_attach_buffer(ring, i);
		if (ret)
			goto out_buffer_fail;
	}

	return 0;

out_buffer_fail:
	for (j = i - 1; j >= 0; j--)
		hns3_free_buffer_detach(ring, j, 0);
	return ret;
}

/* detach a in-used buffer and replace with a reserved one */
static void hns3_replace_buffer(struct hns3_enet_ring *ring, int i,
				struct hns3_desc_cb *res_cb)
{
	hns3_unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc_cb[i] = *res_cb;
	ring->desc_cb[i].refill = 1;
	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma +
					 ring->desc_cb[i].page_offset);
	ring->desc[i].rx.bd_base_info = 0;
}

static void hns3_reuse_buffer(struct hns3_enet_ring *ring, int i)
{
	ring->desc_cb[i].reuse_flag = 0;
	ring->desc_cb[i].refill = 1;
	ring->desc[i].addr = cpu_to_le64(ring->desc_cb[i].dma +
					 ring->desc_cb[i].page_offset);
	ring->desc[i].rx.bd_base_info = 0;

	dma_sync_single_for_device(ring_to_dev(ring),
			ring->desc_cb[i].dma + ring->desc_cb[i].page_offset,
			hns3_buf_size(ring),
			DMA_FROM_DEVICE);
}

static bool hns3_nic_reclaim_desc(struct hns3_enet_ring *ring,
				  int *bytes, int *pkts, int budget)
{
	/* pair with ring->last_to_use update in hns3_tx_doorbell(),
	 * smp_store_release() is not used in hns3_tx_doorbell() because
	 * the doorbell operation already have the needed barrier operation.
	 */
	int ltu = smp_load_acquire(&ring->last_to_use);
	int ntc = ring->next_to_clean;
	struct hns3_desc_cb *desc_cb;
	bool reclaimed = false;
	struct hns3_desc *desc;

	while (ltu != ntc) {
		desc = &ring->desc[ntc];

		if (le16_to_cpu(desc->tx.bdtp_fe_sc_vld_ra_ri) &
				BIT(HNS3_TXD_VLD_B))
			break;

		desc_cb = &ring->desc_cb[ntc];

		if (desc_cb->type & (DESC_TYPE_SKB | DESC_TYPE_BOUNCE_ALL |
				     DESC_TYPE_BOUNCE_HEAD |
				     DESC_TYPE_SGL_SKB)) {
			(*pkts)++;
			(*bytes) += desc_cb->send_bytes;
		}

		/* desc_cb will be cleaned, after hnae3_free_buffer_detach */
		hns3_free_buffer_detach(ring, ntc, budget);

		if (++ntc == ring->desc_num)
			ntc = 0;

		/* Issue prefetch for next Tx descriptor */
		prefetch(&ring->desc_cb[ntc]);
		reclaimed = true;
	}

	if (unlikely(!reclaimed))
		return false;

	/* This smp_store_release() pairs with smp_load_acquire() in
	 * ring_space called by hns3_nic_net_xmit.
	 */
	smp_store_release(&ring->next_to_clean, ntc);

	hns3_tx_spare_update(ring);

	return true;
}

void hns3_clean_tx_ring(struct hns3_enet_ring *ring, int budget)
{
	struct net_device *netdev = ring_to_netdev(ring);
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct netdev_queue *dev_queue;
	int bytes, pkts;

	bytes = 0;
	pkts = 0;

	if (unlikely(!hns3_nic_reclaim_desc(ring, &bytes, &pkts, budget)))
		return;

	ring->tqp_vector->tx_group.total_bytes += bytes;
	ring->tqp_vector->tx_group.total_packets += pkts;

	u64_stats_update_begin(&ring->syncp);
	ring->stats.tx_bytes += bytes;
	ring->stats.tx_pkts += pkts;
	u64_stats_update_end(&ring->syncp);

	dev_queue = netdev_get_tx_queue(netdev, ring->tqp->tqp_index);
	netdev_tx_completed_queue(dev_queue, pkts, bytes);

	if (unlikely(netif_carrier_ok(netdev) &&
		     ring_space(ring) > HNS3_MAX_TSO_BD_NUM)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (netif_tx_queue_stopped(dev_queue) &&
		    !test_bit(HNS3_NIC_STATE_DOWN, &priv->state)) {
			netif_tx_wake_queue(dev_queue);
			ring->stats.restart_queue++;
		}
	}
}

static int hns3_desc_unused(struct hns3_enet_ring *ring)
{
	int ntc = ring->next_to_clean;
	int ntu = ring->next_to_use;

	if (unlikely(ntc == ntu && !ring->desc_cb[ntc].refill))
		return ring->desc_num;

	return ((ntc >= ntu) ? 0 : ring->desc_num) + ntc - ntu;
}

/* Return true if there is any allocation failure */
static bool hns3_nic_alloc_rx_buffers(struct hns3_enet_ring *ring,
				      int cleand_count)
{
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc_cb res_cbs;
	int i, ret;

	for (i = 0; i < cleand_count; i++) {
		desc_cb = &ring->desc_cb[ring->next_to_use];
		if (desc_cb->reuse_flag) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.reuse_pg_cnt++;
			u64_stats_update_end(&ring->syncp);

			hns3_reuse_buffer(ring, ring->next_to_use);
		} else {
			ret = hns3_alloc_and_map_buffer(ring, &res_cbs);
			if (ret) {
				u64_stats_update_begin(&ring->syncp);
				ring->stats.sw_err_cnt++;
				u64_stats_update_end(&ring->syncp);

				hns3_rl_err(ring_to_netdev(ring),
					    "alloc rx buffer failed: %d\n",
					    ret);

				writel(i, ring->tqp->io_base +
				       HNS3_RING_RX_RING_HEAD_REG);
				return true;
			}
			hns3_replace_buffer(ring, ring->next_to_use, &res_cbs);

			u64_stats_update_begin(&ring->syncp);
			ring->stats.non_reuse_pg++;
			u64_stats_update_end(&ring->syncp);
		}

		ring_ptr_move_fw(ring, next_to_use);
	}

	writel(i, ring->tqp->io_base + HNS3_RING_RX_RING_HEAD_REG);
	return false;
}

static bool hns3_can_reuse_page(struct hns3_desc_cb *cb)
{
	return page_count(cb->priv) == cb->pagecnt_bias;
}

static void hns3_nic_reuse_page(struct sk_buff *skb, int i,
				struct hns3_enet_ring *ring, int pull_len,
				struct hns3_desc_cb *desc_cb)
{
	struct hns3_desc *desc = &ring->desc[ring->next_to_clean];
	u32 frag_offset = desc_cb->page_offset + pull_len;
	int size = le16_to_cpu(desc->rx.size);
	u32 truesize = hns3_buf_size(ring);
	u32 frag_size = size - pull_len;
	bool reused;

	if (ring->page_pool) {
		skb_add_rx_frag(skb, i, desc_cb->priv, frag_offset,
				frag_size, truesize);
		return;
	}

	/* Avoid re-using remote or pfmem page */
	if (unlikely(!dev_page_is_reusable(desc_cb->priv)))
		goto out;

	reused = hns3_can_reuse_page(desc_cb);

	/* Rx page can be reused when:
	 * 1. Rx page is only owned by the driver when page_offset
	 *    is zero, which means 0 @ truesize will be used by
	 *    stack after skb_add_rx_frag() is called, and the rest
	 *    of rx page can be reused by driver.
	 * Or
	 * 2. Rx page is only owned by the driver when page_offset
	 *    is non-zero, which means page_offset @ truesize will
	 *    be used by stack after skb_add_rx_frag() is called,
	 *    and 0 @ truesize can be reused by driver.
	 */
	if ((!desc_cb->page_offset && reused) ||
	    ((desc_cb->page_offset + truesize + truesize) <=
	     hns3_page_size(ring) && desc_cb->page_offset)) {
		desc_cb->page_offset += truesize;
		desc_cb->reuse_flag = 1;
	} else if (desc_cb->page_offset && reused) {
		desc_cb->page_offset = 0;
		desc_cb->reuse_flag = 1;
	} else if (frag_size <= ring->rx_copybreak) {
		void *frag = napi_alloc_frag(frag_size);

		if (unlikely(!frag)) {
			u64_stats_update_begin(&ring->syncp);
			ring->stats.frag_alloc_err++;
			u64_stats_update_end(&ring->syncp);

			hns3_rl_err(ring_to_netdev(ring),
				    "failed to allocate rx frag\n");
			goto out;
		}

		desc_cb->reuse_flag = 1;
		memcpy(frag, desc_cb->buf + frag_offset, frag_size);
		skb_add_rx_frag(skb, i, virt_to_page(frag),
				offset_in_page(frag), frag_size, frag_size);

		u64_stats_update_begin(&ring->syncp);
		ring->stats.frag_alloc++;
		u64_stats_update_end(&ring->syncp);
		return;
	}

out:
	desc_cb->pagecnt_bias--;

	if (unlikely(!desc_cb->pagecnt_bias)) {
		page_ref_add(desc_cb->priv, USHRT_MAX);
		desc_cb->pagecnt_bias = USHRT_MAX;
	}

	skb_add_rx_frag(skb, i, desc_cb->priv, frag_offset,
			frag_size, truesize);

	if (unlikely(!desc_cb->reuse_flag))
		__page_frag_cache_drain(desc_cb->priv, desc_cb->pagecnt_bias);
}

static int hns3_gro_complete(struct sk_buff *skb, u32 l234info)
{
	__be16 type = skb->protocol;
	struct tcphdr *th;
	int depth = 0;

	while (eth_type_vlan(type)) {
		struct vlan_hdr *vh;

		if ((depth + VLAN_HLEN) > skb_headlen(skb))
			return -EFAULT;

		vh = (struct vlan_hdr *)(skb->data + depth);
		type = vh->h_vlan_encapsulated_proto;
		depth += VLAN_HLEN;
	}

	skb_set_network_header(skb, depth);

	if (type == htons(ETH_P_IP)) {
		const struct iphdr *iph = ip_hdr(skb);

		depth += sizeof(struct iphdr);
		skb_set_transport_header(skb, depth);
		th = tcp_hdr(skb);
		th->check = ~tcp_v4_check(skb->len - depth, iph->saddr,
					  iph->daddr, 0);
	} else if (type == htons(ETH_P_IPV6)) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);

		depth += sizeof(struct ipv6hdr);
		skb_set_transport_header(skb, depth);
		th = tcp_hdr(skb);
		th->check = ~tcp_v6_check(skb->len - depth, &iph->saddr,
					  &iph->daddr, 0);
	} else {
		hns3_rl_err(skb->dev,
			    "Error: FW GRO supports only IPv4/IPv6, not 0x%04x, depth: %d\n",
			    be16_to_cpu(type), depth);
		return -EFAULT;
	}

	skb_shinfo(skb)->gso_segs = NAPI_GRO_CB(skb)->count;
	if (th->cwr)
		skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

	if (l234info & BIT(HNS3_RXD_GRO_FIXID_B))
		skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_FIXEDID;

	skb->csum_start = (unsigned char *)th - skb->head;
	skb->csum_offset = offsetof(struct tcphdr, check);
	skb->ip_summed = CHECKSUM_PARTIAL;

	trace_hns3_gro(skb);

	return 0;
}

static bool hns3_checksum_complete(struct hns3_enet_ring *ring,
				   struct sk_buff *skb, u32 ptype, u16 csum)
{
	if (ptype == HNS3_INVALID_PTYPE ||
	    hns3_rx_ptype_tbl[ptype].ip_summed != CHECKSUM_COMPLETE)
		return false;

	u64_stats_update_begin(&ring->syncp);
	ring->stats.csum_complete++;
	u64_stats_update_end(&ring->syncp);
	skb->ip_summed = CHECKSUM_COMPLETE;
	skb->csum = csum_unfold((__force __sum16)csum);

	return true;
}

static void hns3_rx_handle_csum(struct sk_buff *skb, u32 l234info,
				u32 ol_info, u32 ptype)
{
	int l3_type, l4_type;
	int ol4_type;

	if (ptype != HNS3_INVALID_PTYPE) {
		skb->csum_level = hns3_rx_ptype_tbl[ptype].csum_level;
		skb->ip_summed = hns3_rx_ptype_tbl[ptype].ip_summed;

		return;
	}

	ol4_type = hnae3_get_field(ol_info, HNS3_RXD_OL4ID_M,
				   HNS3_RXD_OL4ID_S);
	switch (ol4_type) {
	case HNS3_OL4_TYPE_MAC_IN_UDP:
	case HNS3_OL4_TYPE_NVGRE:
		skb->csum_level = 1;
		fallthrough;
	case HNS3_OL4_TYPE_NO_TUN:
		l3_type = hnae3_get_field(l234info, HNS3_RXD_L3ID_M,
					  HNS3_RXD_L3ID_S);
		l4_type = hnae3_get_field(l234info, HNS3_RXD_L4ID_M,
					  HNS3_RXD_L4ID_S);
		/* Can checksum ipv4 or ipv6 + UDP/TCP/SCTP packets */
		if ((l3_type == HNS3_L3_TYPE_IPV4 ||
		     l3_type == HNS3_L3_TYPE_IPV6) &&
		    (l4_type == HNS3_L4_TYPE_UDP ||
		     l4_type == HNS3_L4_TYPE_TCP ||
		     l4_type == HNS3_L4_TYPE_SCTP))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		break;
	default:
		break;
	}
}

static void hns3_rx_checksum(struct hns3_enet_ring *ring, struct sk_buff *skb,
			     u32 l234info, u32 bd_base_info, u32 ol_info,
			     u16 csum)
{
	struct net_device *netdev = ring_to_netdev(ring);
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	u32 ptype = HNS3_INVALID_PTYPE;

	skb->ip_summed = CHECKSUM_NONE;

	skb_checksum_none_assert(skb);

	if (!(netdev->features & NETIF_F_RXCSUM))
		return;

	if (test_bit(HNS3_NIC_STATE_RXD_ADV_LAYOUT_ENABLE, &priv->state))
		ptype = hnae3_get_field(ol_info, HNS3_RXD_PTYPE_M,
					HNS3_RXD_PTYPE_S);

	if (hns3_checksum_complete(ring, skb, ptype, csum))
		return;

	/* check if hardware has done checksum */
	if (!(bd_base_info & BIT(HNS3_RXD_L3L4P_B)))
		return;

	if (unlikely(l234info & (BIT(HNS3_RXD_L3E_B) | BIT(HNS3_RXD_L4E_B) |
				 BIT(HNS3_RXD_OL3E_B) |
				 BIT(HNS3_RXD_OL4E_B)))) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.l3l4_csum_err++;
		u64_stats_update_end(&ring->syncp);

		return;
	}

	hns3_rx_handle_csum(skb, l234info, ol_info, ptype);
}

static void hns3_rx_skb(struct hns3_enet_ring *ring, struct sk_buff *skb)
{
	if (skb_has_frag_list(skb))
		napi_gro_flush(&ring->tqp_vector->napi, false);

	napi_gro_receive(&ring->tqp_vector->napi, skb);
}

static bool hns3_parse_vlan_tag(struct hns3_enet_ring *ring,
				struct hns3_desc *desc, u32 l234info,
				u16 *vlan_tag)
{
	struct hnae3_handle *handle = ring->tqp->handle;
	struct pci_dev *pdev = ring->tqp->handle->pdev;
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);

	if (unlikely(ae_dev->dev_version < HNAE3_DEVICE_VERSION_V2)) {
		*vlan_tag = le16_to_cpu(desc->rx.ot_vlan_tag);
		if (!(*vlan_tag & VLAN_VID_MASK))
			*vlan_tag = le16_to_cpu(desc->rx.vlan_tag);

		return (*vlan_tag != 0);
	}

#define HNS3_STRP_OUTER_VLAN	0x1
#define HNS3_STRP_INNER_VLAN	0x2
#define HNS3_STRP_BOTH		0x3

	/* Hardware always insert VLAN tag into RX descriptor when
	 * remove the tag from packet, driver needs to determine
	 * reporting which tag to stack.
	 */
	switch (hnae3_get_field(l234info, HNS3_RXD_STRP_TAGP_M,
				HNS3_RXD_STRP_TAGP_S)) {
	case HNS3_STRP_OUTER_VLAN:
		if (handle->port_base_vlan_state !=
				HNAE3_PORT_BASE_VLAN_DISABLE)
			return false;

		*vlan_tag = le16_to_cpu(desc->rx.ot_vlan_tag);
		return true;
	case HNS3_STRP_INNER_VLAN:
		if (handle->port_base_vlan_state !=
				HNAE3_PORT_BASE_VLAN_DISABLE)
			return false;

		*vlan_tag = le16_to_cpu(desc->rx.vlan_tag);
		return true;
	case HNS3_STRP_BOTH:
		if (handle->port_base_vlan_state ==
				HNAE3_PORT_BASE_VLAN_DISABLE)
			*vlan_tag = le16_to_cpu(desc->rx.ot_vlan_tag);
		else
			*vlan_tag = le16_to_cpu(desc->rx.vlan_tag);

		return true;
	default:
		return false;
	}
}

static void hns3_rx_ring_move_fw(struct hns3_enet_ring *ring)
{
	ring->desc[ring->next_to_clean].rx.bd_base_info &=
		cpu_to_le32(~BIT(HNS3_RXD_VLD_B));
	ring->desc_cb[ring->next_to_clean].refill = 0;
	ring->next_to_clean += 1;

	if (unlikely(ring->next_to_clean == ring->desc_num))
		ring->next_to_clean = 0;
}

static int hns3_alloc_skb(struct hns3_enet_ring *ring, unsigned int length,
			  unsigned char *va)
{
	struct hns3_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_clean];
	struct net_device *netdev = ring_to_netdev(ring);
	struct sk_buff *skb;

	ring->skb = napi_alloc_skb(&ring->tqp_vector->napi, HNS3_RX_HEAD_SIZE);
	skb = ring->skb;
	if (unlikely(!skb)) {
		hns3_rl_err(netdev, "alloc rx skb fail\n");

		u64_stats_update_begin(&ring->syncp);
		ring->stats.sw_err_cnt++;
		u64_stats_update_end(&ring->syncp);

		return -ENOMEM;
	}

	trace_hns3_rx_desc(ring);
	prefetchw(skb->data);

	ring->pending_buf = 1;
	ring->frag_num = 0;
	ring->tail_skb = NULL;
	if (length <= HNS3_RX_HEAD_SIZE) {
		memcpy(__skb_put(skb, length), va, ALIGN(length, sizeof(long)));

		/* We can reuse buffer as-is, just make sure it is reusable */
		if (dev_page_is_reusable(desc_cb->priv))
			desc_cb->reuse_flag = 1;
		else if (desc_cb->type & DESC_TYPE_PP_FRAG)
			page_pool_put_full_page(ring->page_pool, desc_cb->priv,
						false);
		else /* This page cannot be reused so discard it */
			__page_frag_cache_drain(desc_cb->priv,
						desc_cb->pagecnt_bias);

		hns3_rx_ring_move_fw(ring);
		return 0;
	}

	if (ring->page_pool)
		skb_mark_for_recycle(skb);

	u64_stats_update_begin(&ring->syncp);
	ring->stats.seg_pkt_cnt++;
	u64_stats_update_end(&ring->syncp);

	ring->pull_len = eth_get_headlen(netdev, va, HNS3_RX_HEAD_SIZE);
	__skb_put(skb, ring->pull_len);
	hns3_nic_reuse_page(skb, ring->frag_num++, ring, ring->pull_len,
			    desc_cb);
	hns3_rx_ring_move_fw(ring);

	return 0;
}

static int hns3_add_frag(struct hns3_enet_ring *ring)
{
	struct sk_buff *skb = ring->skb;
	struct sk_buff *head_skb = skb;
	struct sk_buff *new_skb;
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc *desc;
	u32 bd_base_info;

	do {
		desc = &ring->desc[ring->next_to_clean];
		desc_cb = &ring->desc_cb[ring->next_to_clean];
		bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
		/* make sure HW write desc complete */
		dma_rmb();
		if (!(bd_base_info & BIT(HNS3_RXD_VLD_B)))
			return -ENXIO;

		if (unlikely(ring->frag_num >= MAX_SKB_FRAGS)) {
			new_skb = napi_alloc_skb(&ring->tqp_vector->napi, 0);
			if (unlikely(!new_skb)) {
				hns3_rl_err(ring_to_netdev(ring),
					    "alloc rx fraglist skb fail\n");
				return -ENXIO;
			}

			if (ring->page_pool)
				skb_mark_for_recycle(new_skb);

			ring->frag_num = 0;

			if (ring->tail_skb) {
				ring->tail_skb->next = new_skb;
				ring->tail_skb = new_skb;
			} else {
				skb_shinfo(skb)->frag_list = new_skb;
				ring->tail_skb = new_skb;
			}
		}

		if (ring->tail_skb) {
			head_skb->truesize += hns3_buf_size(ring);
			head_skb->data_len += le16_to_cpu(desc->rx.size);
			head_skb->len += le16_to_cpu(desc->rx.size);
			skb = ring->tail_skb;
		}

		dma_sync_single_for_cpu(ring_to_dev(ring),
				desc_cb->dma + desc_cb->page_offset,
				hns3_buf_size(ring),
				DMA_FROM_DEVICE);

		hns3_nic_reuse_page(skb, ring->frag_num++, ring, 0, desc_cb);
		trace_hns3_rx_desc(ring);
		hns3_rx_ring_move_fw(ring);
		ring->pending_buf++;
	} while (!(bd_base_info & BIT(HNS3_RXD_FE_B)));

	return 0;
}

static int hns3_set_gro_and_checksum(struct hns3_enet_ring *ring,
				     struct sk_buff *skb, u32 l234info,
				     u32 bd_base_info, u32 ol_info, u16 csum)
{
	struct net_device *netdev = ring_to_netdev(ring);
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	u32 l3_type;

	skb_shinfo(skb)->gso_size = hnae3_get_field(bd_base_info,
						    HNS3_RXD_GRO_SIZE_M,
						    HNS3_RXD_GRO_SIZE_S);
	/* if there is no HW GRO, do not set gro params */
	if (!skb_shinfo(skb)->gso_size) {
		hns3_rx_checksum(ring, skb, l234info, bd_base_info, ol_info,
				 csum);
		return 0;
	}

	NAPI_GRO_CB(skb)->count = hnae3_get_field(l234info,
						  HNS3_RXD_GRO_COUNT_M,
						  HNS3_RXD_GRO_COUNT_S);

	if (test_bit(HNS3_NIC_STATE_RXD_ADV_LAYOUT_ENABLE, &priv->state)) {
		u32 ptype = hnae3_get_field(ol_info, HNS3_RXD_PTYPE_M,
					    HNS3_RXD_PTYPE_S);

		l3_type = hns3_rx_ptype_tbl[ptype].l3_type;
	} else {
		l3_type = hnae3_get_field(l234info, HNS3_RXD_L3ID_M,
					  HNS3_RXD_L3ID_S);
	}

	if (l3_type == HNS3_L3_TYPE_IPV4)
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
	else if (l3_type == HNS3_L3_TYPE_IPV6)
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
	else
		return -EFAULT;

	return  hns3_gro_complete(skb, l234info);
}

static void hns3_set_rx_skb_rss_type(struct hns3_enet_ring *ring,
				     struct sk_buff *skb, u32 rss_hash)
{
	struct hnae3_handle *handle = ring->tqp->handle;
	enum pkt_hash_types rss_type;

	if (rss_hash)
		rss_type = handle->kinfo.rss_type;
	else
		rss_type = PKT_HASH_TYPE_NONE;

	skb_set_hash(skb, rss_hash, rss_type);
}

static int hns3_handle_bdinfo(struct hns3_enet_ring *ring, struct sk_buff *skb)
{
	struct net_device *netdev = ring_to_netdev(ring);
	enum hns3_pkt_l2t_type l2_frame_type;
	u32 bd_base_info, l234info, ol_info;
	struct hns3_desc *desc;
	unsigned int len;
	int pre_ntc, ret;
	u16 csum;

	/* bdinfo handled below is only valid on the last BD of the
	 * current packet, and ring->next_to_clean indicates the first
	 * descriptor of next packet, so need - 1 below.
	 */
	pre_ntc = ring->next_to_clean ? (ring->next_to_clean - 1) :
					(ring->desc_num - 1);
	desc = &ring->desc[pre_ntc];
	bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
	l234info = le32_to_cpu(desc->rx.l234_info);
	ol_info = le32_to_cpu(desc->rx.ol_info);
	csum = le16_to_cpu(desc->csum);

	if (unlikely(bd_base_info & BIT(HNS3_RXD_TS_VLD_B))) {
		struct hnae3_handle *h = hns3_get_handle(netdev);
		u32 nsec = le32_to_cpu(desc->ts_nsec);
		u32 sec = le32_to_cpu(desc->ts_sec);

		if (h->ae_algo->ops->get_rx_hwts)
			h->ae_algo->ops->get_rx_hwts(h, skb, nsec, sec);
	}

	/* Based on hw strategy, the tag offloaded will be stored at
	 * ot_vlan_tag in two layer tag case, and stored at vlan_tag
	 * in one layer tag case.
	 */
	if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX) {
		u16 vlan_tag;

		if (hns3_parse_vlan_tag(ring, desc, l234info, &vlan_tag))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       vlan_tag);
	}

	if (unlikely(!desc->rx.pkt_len || (l234info & (BIT(HNS3_RXD_TRUNCAT_B) |
				  BIT(HNS3_RXD_L2E_B))))) {
		u64_stats_update_begin(&ring->syncp);
		if (l234info & BIT(HNS3_RXD_L2E_B))
			ring->stats.l2_err++;
		else
			ring->stats.err_pkt_len++;
		u64_stats_update_end(&ring->syncp);

		return -EFAULT;
	}

	len = skb->len;

	/* Do update ip stack process */
	skb->protocol = eth_type_trans(skb, netdev);

	/* This is needed in order to enable forwarding support */
	ret = hns3_set_gro_and_checksum(ring, skb, l234info,
					bd_base_info, ol_info, csum);
	if (unlikely(ret)) {
		u64_stats_update_begin(&ring->syncp);
		ring->stats.rx_err_cnt++;
		u64_stats_update_end(&ring->syncp);
		return ret;
	}

	l2_frame_type = hnae3_get_field(l234info, HNS3_RXD_DMAC_M,
					HNS3_RXD_DMAC_S);

	u64_stats_update_begin(&ring->syncp);
	ring->stats.rx_pkts++;
	ring->stats.rx_bytes += len;

	if (l2_frame_type == HNS3_L2_TYPE_MULTICAST)
		ring->stats.rx_multicast++;

	u64_stats_update_end(&ring->syncp);

	ring->tqp_vector->rx_group.total_bytes += len;

	hns3_set_rx_skb_rss_type(ring, skb, le32_to_cpu(desc->rx.rss_hash));
	return 0;
}

static int hns3_handle_rx_bd(struct hns3_enet_ring *ring)
{
	struct sk_buff *skb = ring->skb;
	struct hns3_desc_cb *desc_cb;
	struct hns3_desc *desc;
	unsigned int length;
	u32 bd_base_info;
	int ret;

	desc = &ring->desc[ring->next_to_clean];
	desc_cb = &ring->desc_cb[ring->next_to_clean];

	prefetch(desc);

	if (!skb) {
		bd_base_info = le32_to_cpu(desc->rx.bd_base_info);
		/* Check valid BD */
		if (unlikely(!(bd_base_info & BIT(HNS3_RXD_VLD_B))))
			return -ENXIO;

		dma_rmb();
		length = le16_to_cpu(desc->rx.size);

		ring->va = desc_cb->buf + desc_cb->page_offset;

		dma_sync_single_for_cpu(ring_to_dev(ring),
				desc_cb->dma + desc_cb->page_offset,
				hns3_buf_size(ring),
				DMA_FROM_DEVICE);

		/* Prefetch first cache line of first page.
		 * Idea is to cache few bytes of the header of the packet.
		 * Our L1 Cache line size is 64B so need to prefetch twice to make
		 * it 128B. But in actual we can have greater size of caches with
		 * 128B Level 1 cache lines. In such a case, single fetch would
		 * suffice to cache in the relevant part of the header.
		 */
		net_prefetch(ring->va);

		ret = hns3_alloc_skb(ring, length, ring->va);
		skb = ring->skb;

		if (ret < 0) /* alloc buffer fail */
			return ret;
		if (!(bd_base_info & BIT(HNS3_RXD_FE_B))) { /* need add frag */
			ret = hns3_add_frag(ring);
			if (ret)
				return ret;
		}
	} else {
		ret = hns3_add_frag(ring);
		if (ret)
			return ret;
	}

	/* As the head data may be changed when GRO enable, copy
	 * the head data in after other data rx completed
	 */
	if (skb->len > HNS3_RX_HEAD_SIZE)
		memcpy(skb->data, ring->va,
		       ALIGN(ring->pull_len, sizeof(long)));

	ret = hns3_handle_bdinfo(ring, skb);
	if (unlikely(ret)) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	skb_record_rx_queue(skb, ring->tqp->tqp_index);
	return 0;
}

int hns3_clean_rx_ring(struct hns3_enet_ring *ring, int budget,
		       void (*rx_fn)(struct hns3_enet_ring *, struct sk_buff *))
{
#define RCB_NOF_ALLOC_RX_BUFF_ONCE 16
	int unused_count = hns3_desc_unused(ring);
	bool failure = false;
	int recv_pkts = 0;
	int err;

	unused_count -= ring->pending_buf;

	while (recv_pkts < budget) {
		/* Reuse or realloc buffers */
		if (unused_count >= RCB_NOF_ALLOC_RX_BUFF_ONCE) {
			failure = failure ||
				hns3_nic_alloc_rx_buffers(ring, unused_count);
			unused_count = 0;
		}

		/* Poll one pkt */
		err = hns3_handle_rx_bd(ring);
		/* Do not get FE for the packet or failed to alloc skb */
		if (unlikely(!ring->skb || err == -ENXIO)) {
			goto out;
		} else if (likely(!err)) {
			rx_fn(ring, ring->skb);
			recv_pkts++;
		}

		unused_count += ring->pending_buf;
		ring->skb = NULL;
		ring->pending_buf = 0;
	}

out:
	return failure ? budget : recv_pkts;
}

static void hns3_update_rx_int_coalesce(struct hns3_enet_tqp_vector *tqp_vector)
{
	struct hns3_enet_ring_group *rx_group = &tqp_vector->rx_group;
	struct dim_sample sample = {};

	if (!rx_group->coal.adapt_enable)
		return;

	dim_update_sample(tqp_vector->event_cnt, rx_group->total_packets,
			  rx_group->total_bytes, &sample);
	net_dim(&rx_group->dim, sample);
}

static void hns3_update_tx_int_coalesce(struct hns3_enet_tqp_vector *tqp_vector)
{
	struct hns3_enet_ring_group *tx_group = &tqp_vector->tx_group;
	struct dim_sample sample = {};

	if (!tx_group->coal.adapt_enable)
		return;

	dim_update_sample(tqp_vector->event_cnt, tx_group->total_packets,
			  tx_group->total_bytes, &sample);
	net_dim(&tx_group->dim, sample);
}

static int hns3_nic_common_poll(struct napi_struct *napi, int budget)
{
	struct hns3_nic_priv *priv = netdev_priv(napi->dev);
	struct hns3_enet_ring *ring;
	int rx_pkt_total = 0;

	struct hns3_enet_tqp_vector *tqp_vector =
		container_of(napi, struct hns3_enet_tqp_vector, napi);
	bool clean_complete = true;
	int rx_budget = budget;

	if (unlikely(test_bit(HNS3_NIC_STATE_DOWN, &priv->state))) {
		napi_complete(napi);
		return 0;
	}

	/* Since the actual Tx work is minimal, we can give the Tx a larger
	 * budget and be more aggressive about cleaning up the Tx descriptors.
	 */
	hns3_for_each_ring(ring, tqp_vector->tx_group)
		hns3_clean_tx_ring(ring, budget);

	/* make sure rx ring budget not smaller than 1 */
	if (tqp_vector->num_tqps > 1)
		rx_budget = max(budget / tqp_vector->num_tqps, 1);

	hns3_for_each_ring(ring, tqp_vector->rx_group) {
		int rx_cleaned = hns3_clean_rx_ring(ring, rx_budget,
						    hns3_rx_skb);
		if (rx_cleaned >= rx_budget)
			clean_complete = false;

		rx_pkt_total += rx_cleaned;
	}

	tqp_vector->rx_group.total_packets += rx_pkt_total;

	if (!clean_complete)
		return budget;

	if (napi_complete(napi) &&
	    likely(!test_bit(HNS3_NIC_STATE_DOWN, &priv->state))) {
		hns3_update_rx_int_coalesce(tqp_vector);
		hns3_update_tx_int_coalesce(tqp_vector);

		hns3_mask_vector_irq(tqp_vector, 1);
	}

	return rx_pkt_total;
}

static int hns3_get_vector_ring_chain(struct hns3_enet_tqp_vector *tqp_vector,
				      struct hnae3_ring_chain_node *head)
{
	struct pci_dev *pdev = tqp_vector->handle->pdev;
	struct hnae3_ring_chain_node *cur_chain = head;
	struct hnae3_ring_chain_node *chain;
	struct hns3_enet_ring *tx_ring;
	struct hns3_enet_ring *rx_ring;

	tx_ring = tqp_vector->tx_group.ring;
	if (tx_ring) {
		cur_chain->tqp_index = tx_ring->tqp->tqp_index;
		hnae3_set_bit(cur_chain->flag, HNAE3_RING_TYPE_B,
			      HNAE3_RING_TYPE_TX);
		hnae3_set_field(cur_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_TX);

		cur_chain->next = NULL;

		while (tx_ring->next) {
			tx_ring = tx_ring->next;

			chain = devm_kzalloc(&pdev->dev, sizeof(*chain),
					     GFP_KERNEL);
			if (!chain)
				goto err_free_chain;

			cur_chain->next = chain;
			chain->tqp_index = tx_ring->tqp->tqp_index;
			hnae3_set_bit(chain->flag, HNAE3_RING_TYPE_B,
				      HNAE3_RING_TYPE_TX);
			hnae3_set_field(chain->int_gl_idx,
					HNAE3_RING_GL_IDX_M,
					HNAE3_RING_GL_IDX_S,
					HNAE3_RING_GL_TX);

			cur_chain = chain;
		}
	}

	rx_ring = tqp_vector->rx_group.ring;
	if (!tx_ring && rx_ring) {
		cur_chain->next = NULL;
		cur_chain->tqp_index = rx_ring->tqp->tqp_index;
		hnae3_set_bit(cur_chain->flag, HNAE3_RING_TYPE_B,
			      HNAE3_RING_TYPE_RX);
		hnae3_set_field(cur_chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_RX);

		rx_ring = rx_ring->next;
	}

	while (rx_ring) {
		chain = devm_kzalloc(&pdev->dev, sizeof(*chain), GFP_KERNEL);
		if (!chain)
			goto err_free_chain;

		cur_chain->next = chain;
		chain->tqp_index = rx_ring->tqp->tqp_index;
		hnae3_set_bit(chain->flag, HNAE3_RING_TYPE_B,
			      HNAE3_RING_TYPE_RX);
		hnae3_set_field(chain->int_gl_idx, HNAE3_RING_GL_IDX_M,
				HNAE3_RING_GL_IDX_S, HNAE3_RING_GL_RX);

		cur_chain = chain;

		rx_ring = rx_ring->next;
	}

	return 0;

err_free_chain:
	cur_chain = head->next;
	while (cur_chain) {
		chain = cur_chain->next;
		devm_kfree(&pdev->dev, cur_chain);
		cur_chain = chain;
	}
	head->next = NULL;

	return -ENOMEM;
}

static void hns3_free_vector_ring_chain(struct hns3_enet_tqp_vector *tqp_vector,
					struct hnae3_ring_chain_node *head)
{
	struct pci_dev *pdev = tqp_vector->handle->pdev;
	struct hnae3_ring_chain_node *chain_tmp, *chain;

	chain = head->next;

	while (chain) {
		chain_tmp = chain->next;
		devm_kfree(&pdev->dev, chain);
		chain = chain_tmp;
	}
}

static void hns3_add_ring_to_group(struct hns3_enet_ring_group *group,
				   struct hns3_enet_ring *ring)
{
	ring->next = group->ring;
	group->ring = ring;

	group->count++;
}

static void hns3_nic_set_cpumask(struct hns3_nic_priv *priv)
{
	struct pci_dev *pdev = priv->ae_handle->pdev;
	struct hns3_enet_tqp_vector *tqp_vector;
	int num_vectors = priv->vector_num;
	int numa_node;
	int vector_i;

	numa_node = dev_to_node(&pdev->dev);

	for (vector_i = 0; vector_i < num_vectors; vector_i++) {
		tqp_vector = &priv->tqp_vector[vector_i];
		cpumask_set_cpu(cpumask_local_spread(vector_i, numa_node),
				&tqp_vector->affinity_mask);
	}
}

static void hns3_rx_dim_work(struct work_struct *work)
{
	struct dim *dim = container_of(work, struct dim, work);
	struct hns3_enet_ring_group *group = container_of(dim,
		struct hns3_enet_ring_group, dim);
	struct hns3_enet_tqp_vector *tqp_vector = group->ring->tqp_vector;
	struct dim_cq_moder cur_moder =
		net_dim_get_rx_moderation(dim->mode, dim->profile_ix);

	hns3_set_vector_coalesce_rx_gl(group->ring->tqp_vector, cur_moder.usec);
	tqp_vector->rx_group.coal.int_gl = cur_moder.usec;

	if (cur_moder.pkts < tqp_vector->rx_group.coal.int_ql_max) {
		hns3_set_vector_coalesce_rx_ql(tqp_vector, cur_moder.pkts);
		tqp_vector->rx_group.coal.int_ql = cur_moder.pkts;
	}

	dim->state = DIM_START_MEASURE;
}

static void hns3_tx_dim_work(struct work_struct *work)
{
	struct dim *dim = container_of(work, struct dim, work);
	struct hns3_enet_ring_group *group = container_of(dim,
		struct hns3_enet_ring_group, dim);
	struct hns3_enet_tqp_vector *tqp_vector = group->ring->tqp_vector;
	struct dim_cq_moder cur_moder =
		net_dim_get_tx_moderation(dim->mode, dim->profile_ix);

	hns3_set_vector_coalesce_tx_gl(tqp_vector, cur_moder.usec);
	tqp_vector->tx_group.coal.int_gl = cur_moder.usec;

	if (cur_moder.pkts < tqp_vector->tx_group.coal.int_ql_max) {
		hns3_set_vector_coalesce_tx_ql(tqp_vector, cur_moder.pkts);
		tqp_vector->tx_group.coal.int_ql = cur_moder.pkts;
	}

	dim->state = DIM_START_MEASURE;
}

static void hns3_nic_init_dim(struct hns3_enet_tqp_vector *tqp_vector)
{
	INIT_WORK(&tqp_vector->rx_group.dim.work, hns3_rx_dim_work);
	INIT_WORK(&tqp_vector->tx_group.dim.work, hns3_tx_dim_work);
}

static int hns3_nic_init_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	int ret;
	int i;

	hns3_nic_set_cpumask(priv);

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		hns3_vector_coalesce_init_hw(tqp_vector, priv);
		tqp_vector->num_tqps = 0;
		hns3_nic_init_dim(tqp_vector);
	}

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		u16 vector_i = i % priv->vector_num;
		u16 tqp_num = h->kinfo.num_tqps;

		tqp_vector = &priv->tqp_vector[vector_i];

		hns3_add_ring_to_group(&tqp_vector->tx_group,
				       &priv->ring[i]);

		hns3_add_ring_to_group(&tqp_vector->rx_group,
				       &priv->ring[i + tqp_num]);

		priv->ring[i].tqp_vector = tqp_vector;
		priv->ring[i + tqp_num].tqp_vector = tqp_vector;
		tqp_vector->num_tqps++;
	}

	for (i = 0; i < priv->vector_num; i++) {
		struct hnae3_ring_chain_node vector_ring_chain;

		tqp_vector = &priv->tqp_vector[i];

		tqp_vector->rx_group.total_bytes = 0;
		tqp_vector->rx_group.total_packets = 0;
		tqp_vector->tx_group.total_bytes = 0;
		tqp_vector->tx_group.total_packets = 0;
		tqp_vector->handle = h;

		ret = hns3_get_vector_ring_chain(tqp_vector,
						 &vector_ring_chain);
		if (ret)
			goto map_ring_fail;

		ret = h->ae_algo->ops->map_ring_to_vector(h,
			tqp_vector->vector_irq, &vector_ring_chain);

		hns3_free_vector_ring_chain(tqp_vector, &vector_ring_chain);

		if (ret)
			goto map_ring_fail;

		netif_napi_add(priv->netdev, &tqp_vector->napi,
			       hns3_nic_common_poll, NAPI_POLL_WEIGHT);
	}

	return 0;

map_ring_fail:
	while (i--)
		netif_napi_del(&priv->tqp_vector[i].napi);

	return ret;
}

static void hns3_nic_init_coal_cfg(struct hns3_nic_priv *priv)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(priv->ae_handle->pdev);
	struct hns3_enet_coalesce *tx_coal = &priv->tx_coal;
	struct hns3_enet_coalesce *rx_coal = &priv->rx_coal;

	/* initialize the configuration for interrupt coalescing.
	 * 1. GL (Interrupt Gap Limiter)
	 * 2. RL (Interrupt Rate Limiter)
	 * 3. QL (Interrupt Quantity Limiter)
	 *
	 * Default: enable interrupt coalescing self-adaptive and GL
	 */
	tx_coal->adapt_enable = 1;
	rx_coal->adapt_enable = 1;

	tx_coal->int_gl = HNS3_INT_GL_50K;
	rx_coal->int_gl = HNS3_INT_GL_50K;

	rx_coal->flow_level = HNS3_FLOW_LOW;
	tx_coal->flow_level = HNS3_FLOW_LOW;

	if (ae_dev->dev_specs.int_ql_max) {
		tx_coal->int_ql = HNS3_INT_QL_DEFAULT_CFG;
		rx_coal->int_ql = HNS3_INT_QL_DEFAULT_CFG;
	}
}

static int hns3_nic_alloc_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	struct hnae3_vector_info *vector;
	struct pci_dev *pdev = h->pdev;
	u16 tqp_num = h->kinfo.num_tqps;
	u16 vector_num;
	int ret = 0;
	u16 i;

	/* RSS size, cpu online and vector_num should be the same */
	/* Should consider 2p/4p later */
	vector_num = min_t(u16, num_online_cpus(), tqp_num);

	vector = devm_kcalloc(&pdev->dev, vector_num, sizeof(*vector),
			      GFP_KERNEL);
	if (!vector)
		return -ENOMEM;

	/* save the actual available vector number */
	vector_num = h->ae_algo->ops->get_vector(h, vector_num, vector);

	priv->vector_num = vector_num;
	priv->tqp_vector = (struct hns3_enet_tqp_vector *)
		devm_kcalloc(&pdev->dev, vector_num, sizeof(*priv->tqp_vector),
			     GFP_KERNEL);
	if (!priv->tqp_vector) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		tqp_vector->idx = i;
		tqp_vector->mask_addr = vector[i].io_addr;
		tqp_vector->vector_irq = vector[i].vector;
		hns3_vector_coalesce_init(tqp_vector, priv);
	}

out:
	devm_kfree(&pdev->dev, vector);
	return ret;
}

static void hns3_clear_ring_group(struct hns3_enet_ring_group *group)
{
	group->ring = NULL;
	group->count = 0;
}

static void hns3_nic_uninit_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_ring_chain_node vector_ring_chain;
	struct hnae3_handle *h = priv->ae_handle;
	struct hns3_enet_tqp_vector *tqp_vector;
	int i;

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];

		if (!tqp_vector->rx_group.ring && !tqp_vector->tx_group.ring)
			continue;

		/* Since the mapping can be overwritten, when fail to get the
		 * chain between vector and ring, we should go on to deal with
		 * the remaining options.
		 */
		if (hns3_get_vector_ring_chain(tqp_vector, &vector_ring_chain))
			dev_warn(priv->dev, "failed to get ring chain\n");

		h->ae_algo->ops->unmap_ring_from_vector(h,
			tqp_vector->vector_irq, &vector_ring_chain);

		hns3_free_vector_ring_chain(tqp_vector, &vector_ring_chain);

		hns3_clear_ring_group(&tqp_vector->rx_group);
		hns3_clear_ring_group(&tqp_vector->tx_group);
		netif_napi_del(&priv->tqp_vector[i].napi);
	}
}

static void hns3_nic_dealloc_vector_data(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct pci_dev *pdev = h->pdev;
	int i, ret;

	for (i = 0; i < priv->vector_num; i++) {
		struct hns3_enet_tqp_vector *tqp_vector;

		tqp_vector = &priv->tqp_vector[i];
		ret = h->ae_algo->ops->put_vector(h, tqp_vector->vector_irq);
		if (ret)
			return;
	}

	devm_kfree(&pdev->dev, priv->tqp_vector);
}

static void hns3_ring_get_cfg(struct hnae3_queue *q, struct hns3_nic_priv *priv,
			      unsigned int ring_type)
{
	int queue_num = priv->ae_handle->kinfo.num_tqps;
	struct hns3_enet_ring *ring;
	int desc_num;

	if (ring_type == HNAE3_RING_TYPE_TX) {
		ring = &priv->ring[q->tqp_index];
		desc_num = priv->ae_handle->kinfo.num_tx_desc;
		ring->queue_index = q->tqp_index;
		ring->tx_copybreak = priv->tx_copybreak;
		ring->last_to_use = 0;
	} else {
		ring = &priv->ring[q->tqp_index + queue_num];
		desc_num = priv->ae_handle->kinfo.num_rx_desc;
		ring->queue_index = q->tqp_index;
		ring->rx_copybreak = priv->rx_copybreak;
	}

	hnae3_set_bit(ring->flag, HNAE3_RING_TYPE_B, ring_type);

	ring->tqp = q;
	ring->desc = NULL;
	ring->desc_cb = NULL;
	ring->dev = priv->dev;
	ring->desc_dma_addr = 0;
	ring->buf_size = q->buf_size;
	ring->desc_num = desc_num;
	ring->next_to_use = 0;
	ring->next_to_clean = 0;
}

static void hns3_queue_to_ring(struct hnae3_queue *tqp,
			       struct hns3_nic_priv *priv)
{
	hns3_ring_get_cfg(tqp, priv, HNAE3_RING_TYPE_TX);
	hns3_ring_get_cfg(tqp, priv, HNAE3_RING_TYPE_RX);
}

static int hns3_get_ring_config(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	struct pci_dev *pdev = h->pdev;
	int i;

	priv->ring = devm_kzalloc(&pdev->dev,
				  array3_size(h->kinfo.num_tqps,
					      sizeof(*priv->ring), 2),
				  GFP_KERNEL);
	if (!priv->ring)
		return -ENOMEM;

	for (i = 0; i < h->kinfo.num_tqps; i++)
		hns3_queue_to_ring(h->kinfo.tqp[i], priv);

	return 0;
}

static void hns3_put_ring_config(struct hns3_nic_priv *priv)
{
	if (!priv->ring)
		return;

	devm_kfree(priv->dev, priv->ring);
	priv->ring = NULL;
}

static void hns3_alloc_page_pool(struct hns3_enet_ring *ring)
{
	struct page_pool_params pp_params = {
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_PAGE_FRAG |
				PP_FLAG_DMA_SYNC_DEV,
		.order = hns3_page_order(ring),
		.pool_size = ring->desc_num * hns3_buf_size(ring) /
				(PAGE_SIZE << hns3_page_order(ring)),
		.nid = dev_to_node(ring_to_dev(ring)),
		.dev = ring_to_dev(ring),
		.dma_dir = DMA_FROM_DEVICE,
		.offset = 0,
		.max_len = PAGE_SIZE << hns3_page_order(ring),
	};

	ring->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(ring->page_pool)) {
		dev_warn(ring_to_dev(ring), "page pool creation failed: %ld\n",
			 PTR_ERR(ring->page_pool));
		ring->page_pool = NULL;
	}
}

static int hns3_alloc_ring_memory(struct hns3_enet_ring *ring)
{
	int ret;

	if (ring->desc_num <= 0 || ring->buf_size <= 0)
		return -EINVAL;

	ring->desc_cb = devm_kcalloc(ring_to_dev(ring), ring->desc_num,
				     sizeof(ring->desc_cb[0]), GFP_KERNEL);
	if (!ring->desc_cb) {
		ret = -ENOMEM;
		goto out;
	}

	ret = hns3_alloc_desc(ring);
	if (ret)
		goto out_with_desc_cb;

	if (!HNAE3_IS_TX_RING(ring)) {
		if (page_pool_enabled)
			hns3_alloc_page_pool(ring);

		ret = hns3_alloc_ring_buffers(ring);
		if (ret)
			goto out_with_desc;
	} else {
		hns3_init_tx_spare_buffer(ring);
	}

	return 0;

out_with_desc:
	hns3_free_desc(ring);
out_with_desc_cb:
	devm_kfree(ring_to_dev(ring), ring->desc_cb);
	ring->desc_cb = NULL;
out:
	return ret;
}

void hns3_fini_ring(struct hns3_enet_ring *ring)
{
	hns3_free_desc(ring);
	devm_kfree(ring_to_dev(ring), ring->desc_cb);
	ring->desc_cb = NULL;
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
	ring->last_to_use = 0;
	ring->pending_buf = 0;
	if (!HNAE3_IS_TX_RING(ring) && ring->skb) {
		dev_kfree_skb_any(ring->skb);
		ring->skb = NULL;
	} else if (HNAE3_IS_TX_RING(ring) && ring->tx_spare) {
		struct hns3_tx_spare *tx_spare = ring->tx_spare;

		dma_unmap_page(ring_to_dev(ring), tx_spare->dma, tx_spare->len,
			       DMA_TO_DEVICE);
		free_pages((unsigned long)tx_spare->buf,
			   get_order(tx_spare->len));
		devm_kfree(ring_to_dev(ring), tx_spare);
		ring->tx_spare = NULL;
	}

	if (!HNAE3_IS_TX_RING(ring) && ring->page_pool) {
		page_pool_destroy(ring->page_pool);
		ring->page_pool = NULL;
	}
}

static int hns3_buf_size2type(u32 buf_size)
{
	int bd_size_type;

	switch (buf_size) {
	case 512:
		bd_size_type = HNS3_BD_SIZE_512_TYPE;
		break;
	case 1024:
		bd_size_type = HNS3_BD_SIZE_1024_TYPE;
		break;
	case 2048:
		bd_size_type = HNS3_BD_SIZE_2048_TYPE;
		break;
	case 4096:
		bd_size_type = HNS3_BD_SIZE_4096_TYPE;
		break;
	default:
		bd_size_type = HNS3_BD_SIZE_2048_TYPE;
	}

	return bd_size_type;
}

static void hns3_init_ring_hw(struct hns3_enet_ring *ring)
{
	dma_addr_t dma = ring->desc_dma_addr;
	struct hnae3_queue *q = ring->tqp;

	if (!HNAE3_IS_TX_RING(ring)) {
		hns3_write_dev(q, HNS3_RING_RX_RING_BASEADDR_L_REG, (u32)dma);
		hns3_write_dev(q, HNS3_RING_RX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns3_write_dev(q, HNS3_RING_RX_RING_BD_LEN_REG,
			       hns3_buf_size2type(ring->buf_size));
		hns3_write_dev(q, HNS3_RING_RX_RING_BD_NUM_REG,
			       ring->desc_num / 8 - 1);
	} else {
		hns3_write_dev(q, HNS3_RING_TX_RING_BASEADDR_L_REG,
			       (u32)dma);
		hns3_write_dev(q, HNS3_RING_TX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns3_write_dev(q, HNS3_RING_TX_RING_BD_NUM_REG,
			       ring->desc_num / 8 - 1);
	}
}

static void hns3_init_tx_ring_tc(struct hns3_nic_priv *priv)
{
	struct hnae3_knic_private_info *kinfo = &priv->ae_handle->kinfo;
	struct hnae3_tc_info *tc_info = &kinfo->tc_info;
	int i;

	for (i = 0; i < tc_info->num_tc; i++) {
		int j;

		for (j = 0; j < tc_info->tqp_count[i]; j++) {
			struct hnae3_queue *q;

			q = priv->ring[tc_info->tqp_offset[i] + j].tqp;
			hns3_write_dev(q, HNS3_RING_TX_RING_TC_REG, i);
		}
	}
}

int hns3_init_all_ring(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int ring_num = h->kinfo.num_tqps * 2;
	int i, j;
	int ret;

	for (i = 0; i < ring_num; i++) {
		ret = hns3_alloc_ring_memory(&priv->ring[i]);
		if (ret) {
			dev_err(priv->dev,
				"Alloc ring memory fail! ret=%d\n", ret);
			goto out_when_alloc_ring_memory;
		}

		u64_stats_init(&priv->ring[i].syncp);
	}

	return 0;

out_when_alloc_ring_memory:
	for (j = i - 1; j >= 0; j--)
		hns3_fini_ring(&priv->ring[j]);

	return -ENOMEM;
}

static void hns3_uninit_all_ring(struct hns3_nic_priv *priv)
{
	struct hnae3_handle *h = priv->ae_handle;
	int i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		hns3_fini_ring(&priv->ring[i]);
		hns3_fini_ring(&priv->ring[i + h->kinfo.num_tqps]);
	}
}

/* Set mac addr if it is configured. or leave it to the AE driver */
static int hns3_init_mac_addr(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	struct hnae3_handle *h = priv->ae_handle;
	u8 mac_addr_temp[ETH_ALEN];
	int ret = 0;

	if (h->ae_algo->ops->get_mac_addr)
		h->ae_algo->ops->get_mac_addr(h, mac_addr_temp);

	/* Check if the MAC address is valid, if not get a random one */
	if (!is_valid_ether_addr(mac_addr_temp)) {
		eth_hw_addr_random(netdev);
		dev_warn(priv->dev, "using random MAC address %pM\n",
			 netdev->dev_addr);
	} else if (!ether_addr_equal(netdev->dev_addr, mac_addr_temp)) {
		eth_hw_addr_set(netdev, mac_addr_temp);
		ether_addr_copy(netdev->perm_addr, mac_addr_temp);
	} else {
		return 0;
	}

	if (h->ae_algo->ops->set_mac_addr)
		ret = h->ae_algo->ops->set_mac_addr(h, netdev->dev_addr, true);

	return ret;
}

static int hns3_init_phy(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	int ret = 0;

	if (h->ae_algo->ops->mac_connect_phy)
		ret = h->ae_algo->ops->mac_connect_phy(h);

	return ret;
}

static void hns3_uninit_phy(struct net_device *netdev)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);

	if (h->ae_algo->ops->mac_disconnect_phy)
		h->ae_algo->ops->mac_disconnect_phy(h);
}

static int hns3_client_start(struct hnae3_handle *handle)
{
	if (!handle->ae_algo->ops->client_start)
		return 0;

	return handle->ae_algo->ops->client_start(handle);
}

static void hns3_client_stop(struct hnae3_handle *handle)
{
	if (!handle->ae_algo->ops->client_stop)
		return;

	handle->ae_algo->ops->client_stop(handle);
}

static void hns3_info_show(struct hns3_nic_priv *priv)
{
	struct hnae3_knic_private_info *kinfo = &priv->ae_handle->kinfo;

	dev_info(priv->dev, "MAC address: %pM\n", priv->netdev->dev_addr);
	dev_info(priv->dev, "Task queue pairs numbers: %u\n", kinfo->num_tqps);
	dev_info(priv->dev, "RSS size: %u\n", kinfo->rss_size);
	dev_info(priv->dev, "Allocated RSS size: %u\n", kinfo->req_rss_size);
	dev_info(priv->dev, "RX buffer length: %u\n", kinfo->rx_buf_len);
	dev_info(priv->dev, "Desc num per TX queue: %u\n", kinfo->num_tx_desc);
	dev_info(priv->dev, "Desc num per RX queue: %u\n", kinfo->num_rx_desc);
	dev_info(priv->dev, "Total number of enabled TCs: %u\n",
		 kinfo->tc_info.num_tc);
	dev_info(priv->dev, "Max mtu size: %u\n", priv->netdev->max_mtu);
}

static void hns3_set_cq_period_mode(struct hns3_nic_priv *priv,
				    enum dim_cq_period_mode mode, bool is_tx)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(priv->ae_handle->pdev);
	struct hnae3_handle *handle = priv->ae_handle;
	int i;

	if (is_tx) {
		priv->tx_cqe_mode = mode;

		for (i = 0; i < priv->vector_num; i++)
			priv->tqp_vector[i].tx_group.dim.mode = mode;
	} else {
		priv->rx_cqe_mode = mode;

		for (i = 0; i < priv->vector_num; i++)
			priv->tqp_vector[i].rx_group.dim.mode = mode;
	}

	/* only device version above V3(include V3), GL can switch CQ/EQ
	 * period mode.
	 */
	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3) {
		u32 new_mode;
		u64 reg;

		new_mode = (mode == DIM_CQ_PERIOD_MODE_START_FROM_CQE) ?
			HNS3_CQ_MODE_CQE : HNS3_CQ_MODE_EQE;
		reg = is_tx ? HNS3_GL1_CQ_MODE_REG : HNS3_GL0_CQ_MODE_REG;

		writel(new_mode, handle->kinfo.io_base + reg);
	}
}

void hns3_cq_period_mode_init(struct hns3_nic_priv *priv,
			      enum dim_cq_period_mode tx_mode,
			      enum dim_cq_period_mode rx_mode)
{
	hns3_set_cq_period_mode(priv, tx_mode, true);
	hns3_set_cq_period_mode(priv, rx_mode, false);
}

static void hns3_state_init(struct hnae3_handle *handle)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);

	set_bit(HNS3_NIC_STATE_INITED, &priv->state);

	if (ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3)
		set_bit(HNAE3_PFLAG_LIMIT_PROMISC, &handle->supported_pflags);

	if (test_bit(HNAE3_DEV_SUPPORT_HW_TX_CSUM_B, ae_dev->caps))
		set_bit(HNS3_NIC_STATE_HW_TX_CSUM_ENABLE, &priv->state);

	if (hnae3_ae_dev_rxd_adv_layout_supported(ae_dev))
		set_bit(HNS3_NIC_STATE_RXD_ADV_LAYOUT_ENABLE, &priv->state);
}

static int hns3_client_init(struct hnae3_handle *handle)
{
	struct pci_dev *pdev = handle->pdev;
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
	u16 alloc_tqps, max_rss_size;
	struct hns3_nic_priv *priv;
	struct net_device *netdev;
	int ret;

	handle->ae_algo->ops->get_tqps_and_rss_info(handle, &alloc_tqps,
						    &max_rss_size);
	netdev = alloc_etherdev_mq(sizeof(struct hns3_nic_priv), alloc_tqps);
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);
	priv->dev = &pdev->dev;
	priv->netdev = netdev;
	priv->ae_handle = handle;
	priv->tx_timeout_count = 0;
	priv->max_non_tso_bd_num = ae_dev->dev_specs.max_non_tso_bd_num;
	set_bit(HNS3_NIC_STATE_DOWN, &priv->state);

	handle->msg_enable = netif_msg_init(debug, DEFAULT_MSG_LEVEL);

	handle->kinfo.netdev = netdev;
	handle->priv = (void *)priv;

	hns3_init_mac_addr(netdev);

	hns3_set_default_feature(netdev);

	netdev->watchdog_timeo = HNS3_TX_TIMEOUT;
	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->netdev_ops = &hns3_nic_netdev_ops;
	SET_NETDEV_DEV(netdev, &pdev->dev);
	hns3_ethtool_set_ops(netdev);

	/* Carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	ret = hns3_get_ring_config(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_get_ring_cfg;
	}

	hns3_nic_init_coal_cfg(priv);

	ret = hns3_nic_alloc_vector_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_alloc_vector_data;
	}

	ret = hns3_nic_init_vector_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_vector_data;
	}

	ret = hns3_init_all_ring(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_ring;
	}

	hns3_cq_period_mode_init(priv, DIM_CQ_PERIOD_MODE_START_FROM_EQE,
				 DIM_CQ_PERIOD_MODE_START_FROM_EQE);

	ret = hns3_init_phy(netdev);
	if (ret)
		goto out_init_phy;

	/* the device can work without cpu rmap, only aRFS needs it */
	ret = hns3_set_rx_cpu_rmap(netdev);
	if (ret)
		dev_warn(priv->dev, "set rx cpu rmap fail, ret=%d\n", ret);

	ret = hns3_nic_init_irq(priv);
	if (ret) {
		dev_err(priv->dev, "init irq failed! ret=%d\n", ret);
		hns3_free_rx_cpu_rmap(netdev);
		goto out_init_irq_fail;
	}

	ret = hns3_client_start(handle);
	if (ret) {
		dev_err(priv->dev, "hns3_client_start fail! ret=%d\n", ret);
		goto out_client_start;
	}

	hns3_dcbnl_setup(handle);

	ret = hns3_dbg_init(handle);
	if (ret) {
		dev_err(priv->dev, "failed to init debugfs, ret = %d\n",
			ret);
		goto out_client_start;
	}

	netdev->max_mtu = HNS3_MAX_MTU(ae_dev->dev_specs.max_frm_size);

	hns3_state_init(handle);

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(priv->dev, "probe register netdev fail!\n");
		goto out_reg_netdev_fail;
	}

	if (netif_msg_drv(handle))
		hns3_info_show(priv);

	return ret;

out_reg_netdev_fail:
	hns3_dbg_uninit(handle);
out_client_start:
	hns3_free_rx_cpu_rmap(netdev);
	hns3_nic_uninit_irq(priv);
out_init_irq_fail:
	hns3_uninit_phy(netdev);
out_init_phy:
	hns3_uninit_all_ring(priv);
out_init_ring:
	hns3_nic_uninit_vector_data(priv);
out_init_vector_data:
	hns3_nic_dealloc_vector_data(priv);
out_alloc_vector_data:
	priv->ring = NULL;
out_get_ring_cfg:
	priv->ae_handle = NULL;
	free_netdev(netdev);
	return ret;
}

static void hns3_client_uninit(struct hnae3_handle *handle, bool reset)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);

	if (netdev->reg_state != NETREG_UNINITIALIZED)
		unregister_netdev(netdev);

	hns3_client_stop(handle);

	hns3_uninit_phy(netdev);

	if (!test_and_clear_bit(HNS3_NIC_STATE_INITED, &priv->state)) {
		netdev_warn(netdev, "already uninitialized\n");
		goto out_netdev_free;
	}

	hns3_free_rx_cpu_rmap(netdev);

	hns3_nic_uninit_irq(priv);

	hns3_clear_all_ring(handle, true);

	hns3_nic_uninit_vector_data(priv);

	hns3_nic_dealloc_vector_data(priv);

	hns3_uninit_all_ring(priv);

	hns3_put_ring_config(priv);

out_netdev_free:
	hns3_dbg_uninit(handle);
	free_netdev(netdev);
}

static void hns3_link_status_change(struct hnae3_handle *handle, bool linkup)
{
	struct net_device *netdev = handle->kinfo.netdev;

	if (!netdev)
		return;

	if (linkup) {
		netif_tx_wake_all_queues(netdev);
		netif_carrier_on(netdev);
		if (netif_msg_link(handle))
			netdev_info(netdev, "link up\n");
	} else {
		netif_carrier_off(netdev);
		netif_tx_stop_all_queues(netdev);
		if (netif_msg_link(handle))
			netdev_info(netdev, "link down\n");
	}
}

static void hns3_clear_tx_ring(struct hns3_enet_ring *ring)
{
	while (ring->next_to_clean != ring->next_to_use) {
		ring->desc[ring->next_to_clean].tx.bdtp_fe_sc_vld_ra_ri = 0;
		hns3_free_buffer_detach(ring, ring->next_to_clean, 0);
		ring_ptr_move_fw(ring, next_to_clean);
	}

	ring->pending_buf = 0;
}

static int hns3_clear_rx_ring(struct hns3_enet_ring *ring)
{
	struct hns3_desc_cb res_cbs;
	int ret;

	while (ring->next_to_use != ring->next_to_clean) {
		/* When a buffer is not reused, it's memory has been
		 * freed in hns3_handle_rx_bd or will be freed by
		 * stack, so we need to replace the buffer here.
		 */
		if (!ring->desc_cb[ring->next_to_use].reuse_flag) {
			ret = hns3_alloc_and_map_buffer(ring, &res_cbs);
			if (ret) {
				u64_stats_update_begin(&ring->syncp);
				ring->stats.sw_err_cnt++;
				u64_stats_update_end(&ring->syncp);
				/* if alloc new buffer fail, exit directly
				 * and reclear in up flow.
				 */
				netdev_warn(ring_to_netdev(ring),
					    "reserve buffer map failed, ret = %d\n",
					    ret);
				return ret;
			}
			hns3_replace_buffer(ring, ring->next_to_use, &res_cbs);
		}
		ring_ptr_move_fw(ring, next_to_use);
	}

	/* Free the pending skb in rx ring */
	if (ring->skb) {
		dev_kfree_skb_any(ring->skb);
		ring->skb = NULL;
		ring->pending_buf = 0;
	}

	return 0;
}

static void hns3_force_clear_rx_ring(struct hns3_enet_ring *ring)
{
	while (ring->next_to_use != ring->next_to_clean) {
		/* When a buffer is not reused, it's memory has been
		 * freed in hns3_handle_rx_bd or will be freed by
		 * stack, so only need to unmap the buffer here.
		 */
		if (!ring->desc_cb[ring->next_to_use].reuse_flag) {
			hns3_unmap_buffer(ring,
					  &ring->desc_cb[ring->next_to_use]);
			ring->desc_cb[ring->next_to_use].dma = 0;
		}

		ring_ptr_move_fw(ring, next_to_use);
	}
}

static void hns3_clear_all_ring(struct hnae3_handle *h, bool force)
{
	struct net_device *ndev = h->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	u32 i;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		struct hns3_enet_ring *ring;

		ring = &priv->ring[i];
		hns3_clear_tx_ring(ring);

		ring = &priv->ring[i + h->kinfo.num_tqps];
		/* Continue to clear other rings even if clearing some
		 * rings failed.
		 */
		if (force)
			hns3_force_clear_rx_ring(ring);
		else
			hns3_clear_rx_ring(ring);
	}
}

int hns3_nic_reset_all_ring(struct hnae3_handle *h)
{
	struct net_device *ndev = h->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);
	struct hns3_enet_ring *rx_ring;
	int i, j;
	int ret;

	ret = h->ae_algo->ops->reset_queue(h);
	if (ret)
		return ret;

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		hns3_init_ring_hw(&priv->ring[i]);

		/* We need to clear tx ring here because self test will
		 * use the ring and will not run down before up
		 */
		hns3_clear_tx_ring(&priv->ring[i]);
		priv->ring[i].next_to_clean = 0;
		priv->ring[i].next_to_use = 0;
		priv->ring[i].last_to_use = 0;

		rx_ring = &priv->ring[i + h->kinfo.num_tqps];
		hns3_init_ring_hw(rx_ring);
		ret = hns3_clear_rx_ring(rx_ring);
		if (ret)
			return ret;

		/* We can not know the hardware head and tail when this
		 * function is called in reset flow, so we reuse all desc.
		 */
		for (j = 0; j < rx_ring->desc_num; j++)
			hns3_reuse_buffer(rx_ring, j);

		rx_ring->next_to_clean = 0;
		rx_ring->next_to_use = 0;
	}

	hns3_init_tx_ring_tc(priv);

	return 0;
}

static int hns3_reset_notify_down_enet(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct net_device *ndev = kinfo->netdev;
	struct hns3_nic_priv *priv = netdev_priv(ndev);

	if (test_and_set_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
		return 0;

	if (!netif_running(ndev))
		return 0;

	return hns3_nic_net_stop(ndev);
}

static int hns3_reset_notify_up_enet(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hns3_nic_priv *priv = netdev_priv(kinfo->netdev);
	int ret = 0;

	if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state)) {
		netdev_err(kinfo->netdev, "device is not initialized yet\n");
		return -EFAULT;
	}

	clear_bit(HNS3_NIC_STATE_RESETTING, &priv->state);

	if (netif_running(kinfo->netdev)) {
		ret = hns3_nic_net_open(kinfo->netdev);
		if (ret) {
			set_bit(HNS3_NIC_STATE_RESETTING, &priv->state);
			netdev_err(kinfo->netdev,
				   "net up fail, ret=%d!\n", ret);
			return ret;
		}
	}

	return ret;
}

static int hns3_reset_notify_init_enet(struct hnae3_handle *handle)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);
	int ret;

	/* Carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	ret = hns3_get_ring_config(priv);
	if (ret)
		return ret;

	ret = hns3_nic_alloc_vector_data(priv);
	if (ret)
		goto err_put_ring;

	ret = hns3_nic_init_vector_data(priv);
	if (ret)
		goto err_dealloc_vector;

	ret = hns3_init_all_ring(priv);
	if (ret)
		goto err_uninit_vector;

	hns3_cq_period_mode_init(priv, priv->tx_cqe_mode, priv->rx_cqe_mode);

	/* the device can work without cpu rmap, only aRFS needs it */
	ret = hns3_set_rx_cpu_rmap(netdev);
	if (ret)
		dev_warn(priv->dev, "set rx cpu rmap fail, ret=%d\n", ret);

	ret = hns3_nic_init_irq(priv);
	if (ret) {
		dev_err(priv->dev, "init irq failed! ret=%d\n", ret);
		hns3_free_rx_cpu_rmap(netdev);
		goto err_init_irq_fail;
	}

	if (!hns3_is_phys_func(handle->pdev))
		hns3_init_mac_addr(netdev);

	ret = hns3_client_start(handle);
	if (ret) {
		dev_err(priv->dev, "hns3_client_start fail! ret=%d\n", ret);
		goto err_client_start_fail;
	}

	set_bit(HNS3_NIC_STATE_INITED, &priv->state);

	return ret;

err_client_start_fail:
	hns3_free_rx_cpu_rmap(netdev);
	hns3_nic_uninit_irq(priv);
err_init_irq_fail:
	hns3_uninit_all_ring(priv);
err_uninit_vector:
	hns3_nic_uninit_vector_data(priv);
err_dealloc_vector:
	hns3_nic_dealloc_vector_data(priv);
err_put_ring:
	hns3_put_ring_config(priv);

	return ret;
}

static int hns3_reset_notify_uninit_enet(struct hnae3_handle *handle)
{
	struct net_device *netdev = handle->kinfo.netdev;
	struct hns3_nic_priv *priv = netdev_priv(netdev);

	if (!test_and_clear_bit(HNS3_NIC_STATE_INITED, &priv->state)) {
		netdev_warn(netdev, "already uninitialized\n");
		return 0;
	}

	hns3_free_rx_cpu_rmap(netdev);
	hns3_nic_uninit_irq(priv);
	hns3_clear_all_ring(handle, true);
	hns3_reset_tx_queue(priv->ae_handle);

	hns3_nic_uninit_vector_data(priv);

	hns3_nic_dealloc_vector_data(priv);

	hns3_uninit_all_ring(priv);

	hns3_put_ring_config(priv);

	return 0;
}

static int hns3_reset_notify(struct hnae3_handle *handle,
			     enum hnae3_reset_notify_type type)
{
	int ret = 0;

	switch (type) {
	case HNAE3_UP_CLIENT:
		ret = hns3_reset_notify_up_enet(handle);
		break;
	case HNAE3_DOWN_CLIENT:
		ret = hns3_reset_notify_down_enet(handle);
		break;
	case HNAE3_INIT_CLIENT:
		ret = hns3_reset_notify_init_enet(handle);
		break;
	case HNAE3_UNINIT_CLIENT:
		ret = hns3_reset_notify_uninit_enet(handle);
		break;
	default:
		break;
	}

	return ret;
}

static int hns3_change_channels(struct hnae3_handle *handle, u32 new_tqp_num,
				bool rxfh_configured)
{
	int ret;

	ret = handle->ae_algo->ops->set_channels(handle, new_tqp_num,
						 rxfh_configured);
	if (ret) {
		dev_err(&handle->pdev->dev,
			"Change tqp num(%u) fail.\n", new_tqp_num);
		return ret;
	}

	ret = hns3_reset_notify(handle, HNAE3_INIT_CLIENT);
	if (ret)
		return ret;

	ret =  hns3_reset_notify(handle, HNAE3_UP_CLIENT);
	if (ret)
		hns3_reset_notify(handle, HNAE3_UNINIT_CLIENT);

	return ret;
}

int hns3_set_channels(struct net_device *netdev,
		      struct ethtool_channels *ch)
{
	struct hnae3_handle *h = hns3_get_handle(netdev);
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	bool rxfh_configured = netif_is_rxfh_configured(netdev);
	u32 new_tqp_num = ch->combined_count;
	u16 org_tqp_num;
	int ret;

	if (hns3_nic_resetting(netdev))
		return -EBUSY;

	if (ch->rx_count || ch->tx_count)
		return -EINVAL;

	if (kinfo->tc_info.mqprio_active) {
		dev_err(&netdev->dev,
			"it's not allowed to set channels via ethtool when MQPRIO mode is on\n");
		return -EINVAL;
	}

	if (new_tqp_num > hns3_get_max_available_channels(h) ||
	    new_tqp_num < 1) {
		dev_err(&netdev->dev,
			"Change tqps fail, the tqp range is from 1 to %u",
			hns3_get_max_available_channels(h));
		return -EINVAL;
	}

	if (kinfo->rss_size == new_tqp_num)
		return 0;

	netif_dbg(h, drv, netdev,
		  "set channels: tqp_num=%u, rxfh=%d\n",
		  new_tqp_num, rxfh_configured);

	ret = hns3_reset_notify(h, HNAE3_DOWN_CLIENT);
	if (ret)
		return ret;

	ret = hns3_reset_notify(h, HNAE3_UNINIT_CLIENT);
	if (ret)
		return ret;

	org_tqp_num = h->kinfo.num_tqps;
	ret = hns3_change_channels(h, new_tqp_num, rxfh_configured);
	if (ret) {
		int ret1;

		netdev_warn(netdev,
			    "Change channels fail, revert to old value\n");
		ret1 = hns3_change_channels(h, org_tqp_num, rxfh_configured);
		if (ret1) {
			netdev_err(netdev,
				   "revert to old channel fail\n");
			return ret1;
		}

		return ret;
	}

	return 0;
}

static const struct hns3_hw_error_info hns3_hw_err[] = {
	{ .type = HNAE3_PPU_POISON_ERROR,
	  .msg = "PPU poison" },
	{ .type = HNAE3_CMDQ_ECC_ERROR,
	  .msg = "IMP CMDQ error" },
	{ .type = HNAE3_IMP_RD_POISON_ERROR,
	  .msg = "IMP RD poison" },
	{ .type = HNAE3_ROCEE_AXI_RESP_ERROR,
	  .msg = "ROCEE AXI RESP error" },
};

static void hns3_process_hw_error(struct hnae3_handle *handle,
				  enum hnae3_hw_error_type type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hns3_hw_err); i++) {
		if (hns3_hw_err[i].type == type) {
			dev_err(&handle->pdev->dev, "Detected %s!\n",
				hns3_hw_err[i].msg);
			break;
		}
	}
}

static const struct hnae3_client_ops client_ops = {
	.init_instance = hns3_client_init,
	.uninit_instance = hns3_client_uninit,
	.link_status_change = hns3_link_status_change,
	.reset_notify = hns3_reset_notify,
	.process_hw_error = hns3_process_hw_error,
};

/* hns3_init_module - Driver registration routine
 * hns3_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init hns3_init_module(void)
{
	int ret;

	pr_info("%s: %s - version\n", hns3_driver_name, hns3_driver_string);
	pr_info("%s: %s\n", hns3_driver_name, hns3_copyright);

	client.type = HNAE3_CLIENT_KNIC;
	snprintf(client.name, HNAE3_CLIENT_NAME_LENGTH, "%s",
		 hns3_driver_name);

	client.ops = &client_ops;

	INIT_LIST_HEAD(&client.node);

	hns3_dbg_register_debugfs(hns3_driver_name);

	ret = hnae3_register_client(&client);
	if (ret)
		goto err_reg_client;

	ret = pci_register_driver(&hns3_driver);
	if (ret)
		goto err_reg_driver;

	return ret;

err_reg_driver:
	hnae3_unregister_client(&client);
err_reg_client:
	hns3_dbg_unregister_debugfs();
	return ret;
}
module_init(hns3_init_module);

/* hns3_exit_module - Driver exit cleanup routine
 * hns3_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit hns3_exit_module(void)
{
	pci_unregister_driver(&hns3_driver);
	hnae3_unregister_client(&client);
	hns3_dbg_unregister_debugfs();
}
module_exit(hns3_exit_module);

MODULE_DESCRIPTION("HNS3: Hisilicon Ethernet Driver");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("pci:hns-nic");
