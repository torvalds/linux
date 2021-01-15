// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright 2020-2021 NXP Semiconductors
 */
#include <net/devlink.h>
#include "ocelot.h"

/* The queue system tracks four resource consumptions:
 * Resource 0: Memory tracked per source port
 * Resource 1: Frame references tracked per source port
 * Resource 2: Memory tracked per destination port
 * Resource 3: Frame references tracked per destination port
 */
#define OCELOT_RESOURCE_SZ		256
#define OCELOT_NUM_RESOURCES		4

#define BUF_xxxx_I			(0 * OCELOT_RESOURCE_SZ)
#define REF_xxxx_I			(1 * OCELOT_RESOURCE_SZ)
#define BUF_xxxx_E			(2 * OCELOT_RESOURCE_SZ)
#define REF_xxxx_E			(3 * OCELOT_RESOURCE_SZ)

/* For each resource type there are 4 types of watermarks:
 * Q_RSRV: reservation per QoS class per port
 * PRIO_SHR: sharing watermark per QoS class across all ports
 * P_RSRV: reservation per port
 * COL_SHR: sharing watermark per color (drop precedence) across all ports
 */
#define xxx_Q_RSRV_x			0
#define xxx_PRIO_SHR_x			216
#define xxx_P_RSRV_x			224
#define xxx_COL_SHR_x			254

/* Reservation Watermarks
 * ----------------------
 *
 * For setting up the reserved areas, egress watermarks exist per port and per
 * QoS class for both ingress and egress.
 */

/*  Amount of packet buffer
 *  |  per QoS class
 *  |  |  reserved
 *  |  |  |   per egress port
 *  |  |  |   |
 *  V  V  v   v
 * BUF_Q_RSRV_E
 */
#define BUF_Q_RSRV_E(port, prio) \
	(BUF_xxxx_E + xxx_Q_RSRV_x + OCELOT_NUM_TC * (port) + (prio))

/*  Amount of packet buffer
 *  |  for all port's traffic classes
 *  |  |  reserved
 *  |  |  |   per egress port
 *  |  |  |   |
 *  V  V  v   v
 * BUF_P_RSRV_E
 */
#define BUF_P_RSRV_E(port) \
	(BUF_xxxx_E + xxx_P_RSRV_x + (port))

/*  Amount of packet buffer
 *  |  per QoS class
 *  |  |  reserved
 *  |  |  |   per ingress port
 *  |  |  |   |
 *  V  V  v   v
 * BUF_Q_RSRV_I
 */
#define BUF_Q_RSRV_I(port, prio) \
	(BUF_xxxx_I + xxx_Q_RSRV_x + OCELOT_NUM_TC * (port) + (prio))

/*  Amount of packet buffer
 *  |  for all port's traffic classes
 *  |  |  reserved
 *  |  |  |   per ingress port
 *  |  |  |   |
 *  V  V  v   v
 * BUF_P_RSRV_I
 */
#define BUF_P_RSRV_I(port) \
	(BUF_xxxx_I + xxx_P_RSRV_x + (port))

/*  Amount of frame references
 *  |  per QoS class
 *  |  |  reserved
 *  |  |  |   per egress port
 *  |  |  |   |
 *  V  V  v   v
 * REF_Q_RSRV_E
 */
#define REF_Q_RSRV_E(port, prio) \
	(REF_xxxx_E + xxx_Q_RSRV_x + OCELOT_NUM_TC * (port) + (prio))

/*  Amount of frame references
 *  |  for all port's traffic classes
 *  |  |  reserved
 *  |  |  |   per egress port
 *  |  |  |   |
 *  V  V  v   v
 * REF_P_RSRV_E
 */
#define REF_P_RSRV_E(port) \
	(REF_xxxx_E + xxx_P_RSRV_x + (port))

/*  Amount of frame references
 *  |  per QoS class
 *  |  |  reserved
 *  |  |  |   per ingress port
 *  |  |  |   |
 *  V  V  v   v
 * REF_Q_RSRV_I
 */
#define REF_Q_RSRV_I(port, prio) \
	(REF_xxxx_I + xxx_Q_RSRV_x + OCELOT_NUM_TC * (port) + (prio))

/*  Amount of frame references
 *  |  for all port's traffic classes
 *  |  |  reserved
 *  |  |  |   per ingress port
 *  |  |  |   |
 *  V  V  v   v
 * REF_P_RSRV_I
 */
#define REF_P_RSRV_I(port) \
	(REF_xxxx_I + xxx_P_RSRV_x + (port))

/* Sharing Watermarks
 * ------------------
 *
 * The shared memory area is shared between all ports.
 */

/* Amount of buffer
 *  |   per QoS class
 *  |   |    from the shared memory area
 *  |   |    |  for egress traffic
 *  |   |    |  |
 *  V   V    v  v
 * BUF_PRIO_SHR_E
 */
#define BUF_PRIO_SHR_E(prio) \
	(BUF_xxxx_E + xxx_PRIO_SHR_x + (prio))

/* Amount of buffer
 *  |   per color (drop precedence level)
 *  |   |   from the shared memory area
 *  |   |   |  for egress traffic
 *  |   |   |  |
 *  V   V   v  v
 * BUF_COL_SHR_E
 */
#define BUF_COL_SHR_E(dp) \
	(BUF_xxxx_E + xxx_COL_SHR_x + (1 - (dp)))

/* Amount of buffer
 *  |   per QoS class
 *  |   |    from the shared memory area
 *  |   |    |  for ingress traffic
 *  |   |    |  |
 *  V   V    v  v
 * BUF_PRIO_SHR_I
 */
#define BUF_PRIO_SHR_I(prio) \
	(BUF_xxxx_I + xxx_PRIO_SHR_x + (prio))

/* Amount of buffer
 *  |   per color (drop precedence level)
 *  |   |   from the shared memory area
 *  |   |   |  for ingress traffic
 *  |   |   |  |
 *  V   V   v  v
 * BUF_COL_SHR_I
 */
#define BUF_COL_SHR_I(dp) \
	(BUF_xxxx_I + xxx_COL_SHR_x + (1 - (dp)))

/* Amount of frame references
 *  |   per QoS class
 *  |   |    from the shared area
 *  |   |    |  for egress traffic
 *  |   |    |  |
 *  V   V    v  v
 * REF_PRIO_SHR_E
 */
#define REF_PRIO_SHR_E(prio) \
	(REF_xxxx_E + xxx_PRIO_SHR_x + (prio))

/* Amount of frame references
 *  |   per color (drop precedence level)
 *  |   |   from the shared area
 *  |   |   |  for egress traffic
 *  |   |   |  |
 *  V   V   v  v
 * REF_COL_SHR_E
 */
#define REF_COL_SHR_E(dp) \
	(REF_xxxx_E + xxx_COL_SHR_x + (1 - (dp)))

/* Amount of frame references
 *  |   per QoS class
 *  |   |    from the shared area
 *  |   |    |  for ingress traffic
 *  |   |    |  |
 *  V   V    v  v
 * REF_PRIO_SHR_I
 */
#define REF_PRIO_SHR_I(prio) \
	(REF_xxxx_I + xxx_PRIO_SHR_x + (prio))

/* Amount of frame references
 *  |   per color (drop precedence level)
 *  |   |   from the shared area
 *  |   |   |  for ingress traffic
 *  |   |   |  |
 *  V   V   v  v
 * REF_COL_SHR_I
 */
#define REF_COL_SHR_I(dp) \
	(REF_xxxx_I + xxx_COL_SHR_x + (1 - (dp)))

static u32 ocelot_wm_read(struct ocelot *ocelot, int index)
{
	int wm = ocelot_read_gix(ocelot, QSYS_RES_CFG, index);

	return ocelot->ops->wm_dec(wm);
}

static void ocelot_wm_write(struct ocelot *ocelot, int index, u32 val)
{
	u32 wm = ocelot->ops->wm_enc(val);

	ocelot_write_gix(ocelot, wm, QSYS_RES_CFG, index);
}

/* The hardware comes out of reset with strange defaults: the sum of all
 * reservations for frame memory is larger than the total buffer size.
 * One has to wonder how can the reservation watermarks still guarantee
 * anything under congestion.
 * Bring some sense into the hardware by changing the defaults to disable all
 * reservations and rely only on the sharing watermark for frames with drop
 * precedence 0. The user can still explicitly request reservations per port
 * and per port-tc through devlink-sb.
 */
static void ocelot_disable_reservation_watermarks(struct ocelot *ocelot,
						  int port)
{
	int prio;

	for (prio = 0; prio < OCELOT_NUM_TC; prio++) {
		ocelot_wm_write(ocelot, BUF_Q_RSRV_I(port, prio), 0);
		ocelot_wm_write(ocelot, BUF_Q_RSRV_E(port, prio), 0);
		ocelot_wm_write(ocelot, REF_Q_RSRV_I(port, prio), 0);
		ocelot_wm_write(ocelot, REF_Q_RSRV_E(port, prio), 0);
	}

	ocelot_wm_write(ocelot, BUF_P_RSRV_I(port), 0);
	ocelot_wm_write(ocelot, BUF_P_RSRV_E(port), 0);
	ocelot_wm_write(ocelot, REF_P_RSRV_I(port), 0);
	ocelot_wm_write(ocelot, REF_P_RSRV_E(port), 0);
}

/* We want the sharing watermarks to consume all nonreserved resources, for
 * efficient resource utilization (a single traffic flow should be able to use
 * up the entire buffer space and frame resources as long as there's no
 * interference).
 * The switch has 10 sharing watermarks per lookup: 8 per traffic class and 2
 * per color (drop precedence).
 * The trouble with configuring these sharing watermarks is that:
 * (1) There's a risk that we overcommit the resources if we configure
 *     (a) all 8 per-TC sharing watermarks to the max
 *     (b) all 2 per-color sharing watermarks to the max
 * (2) There's a risk that we undercommit the resources if we configure
 *     (a) all 8 per-TC sharing watermarks to "max / 8"
 *     (b) all 2 per-color sharing watermarks to "max / 2"
 * So for Linux, let's just disable the sharing watermarks per traffic class
 * (setting them to 0 will make them always exceeded), and rely only on the
 * sharing watermark for drop priority 0. So frames with drop priority set to 1
 * by QoS classification or policing will still be allowed, but only as long as
 * the port and port-TC reservations are not exceeded.
 */
static void ocelot_disable_tc_sharing_watermarks(struct ocelot *ocelot)
{
	int prio;

	for (prio = 0; prio < OCELOT_NUM_TC; prio++) {
		ocelot_wm_write(ocelot, BUF_PRIO_SHR_I(prio), 0);
		ocelot_wm_write(ocelot, BUF_PRIO_SHR_E(prio), 0);
		ocelot_wm_write(ocelot, REF_PRIO_SHR_I(prio), 0);
		ocelot_wm_write(ocelot, REF_PRIO_SHR_E(prio), 0);
	}
}

static void ocelot_get_buf_rsrv(struct ocelot *ocelot, u32 *buf_rsrv_i,
				u32 *buf_rsrv_e)
{
	int port, prio;

	*buf_rsrv_i = 0;
	*buf_rsrv_e = 0;

	for (port = 0; port <= ocelot->num_phys_ports; port++) {
		for (prio = 0; prio < OCELOT_NUM_TC; prio++) {
			*buf_rsrv_i += ocelot_wm_read(ocelot,
						      BUF_Q_RSRV_I(port, prio));
			*buf_rsrv_e += ocelot_wm_read(ocelot,
						      BUF_Q_RSRV_E(port, prio));
		}

		*buf_rsrv_i += ocelot_wm_read(ocelot, BUF_P_RSRV_I(port));
		*buf_rsrv_e += ocelot_wm_read(ocelot, BUF_P_RSRV_E(port));
	}

	*buf_rsrv_i *= OCELOT_BUFFER_CELL_SZ;
	*buf_rsrv_e *= OCELOT_BUFFER_CELL_SZ;
}

static void ocelot_get_ref_rsrv(struct ocelot *ocelot, u32 *ref_rsrv_i,
				u32 *ref_rsrv_e)
{
	int port, prio;

	*ref_rsrv_i = 0;
	*ref_rsrv_e = 0;

	for (port = 0; port <= ocelot->num_phys_ports; port++) {
		for (prio = 0; prio < OCELOT_NUM_TC; prio++) {
			*ref_rsrv_i += ocelot_wm_read(ocelot,
						      REF_Q_RSRV_I(port, prio));
			*ref_rsrv_e += ocelot_wm_read(ocelot,
						      REF_Q_RSRV_E(port, prio));
		}

		*ref_rsrv_i += ocelot_wm_read(ocelot, REF_P_RSRV_I(port));
		*ref_rsrv_e += ocelot_wm_read(ocelot, REF_P_RSRV_E(port));
	}
}

/* Calculate all reservations, then set up the sharing watermark for DP=0 to
 * consume the remaining resources up to the pool's configured size.
 */
static void ocelot_setup_sharing_watermarks(struct ocelot *ocelot)
{
	u32 buf_rsrv_i, buf_rsrv_e;
	u32 ref_rsrv_i, ref_rsrv_e;
	u32 buf_shr_i, buf_shr_e;
	u32 ref_shr_i, ref_shr_e;

	ocelot_get_buf_rsrv(ocelot, &buf_rsrv_i, &buf_rsrv_e);
	ocelot_get_ref_rsrv(ocelot, &ref_rsrv_i, &ref_rsrv_e);

	buf_shr_i = ocelot->packet_buffer_size - buf_rsrv_i;
	buf_shr_e = ocelot->packet_buffer_size - buf_rsrv_e;
	ref_shr_i = ocelot->num_frame_refs - ref_rsrv_i;
	ref_shr_e = ocelot->num_frame_refs - ref_rsrv_e;

	buf_shr_i /= OCELOT_BUFFER_CELL_SZ;
	buf_shr_e /= OCELOT_BUFFER_CELL_SZ;

	ocelot_wm_write(ocelot, BUF_COL_SHR_I(0), buf_shr_i);
	ocelot_wm_write(ocelot, BUF_COL_SHR_E(0), buf_shr_e);
	ocelot_wm_write(ocelot, REF_COL_SHR_E(0), ref_shr_e);
	ocelot_wm_write(ocelot, REF_COL_SHR_I(0), ref_shr_i);
	ocelot_wm_write(ocelot, BUF_COL_SHR_I(1), 0);
	ocelot_wm_write(ocelot, BUF_COL_SHR_E(1), 0);
	ocelot_wm_write(ocelot, REF_COL_SHR_E(1), 0);
	ocelot_wm_write(ocelot, REF_COL_SHR_I(1), 0);
}

/* The hardware works like this:
 *
 *                         Frame forwarding decision taken
 *                                       |
 *                                       v
 *       +--------------------+--------------------+--------------------+
 *       |                    |                    |                    |
 *       v                    v                    v                    v
 * Ingress memory       Egress memory        Ingress frame        Egress frame
 *     check                check           reference check      reference check
 *       |                    |                    |                    |
 *       v                    v                    v                    v
 *  BUF_Q_RSRV_I   ok    BUF_Q_RSRV_E   ok    REF_Q_RSRV_I   ok     REF_Q_RSRV_E   ok
 *(src port, prio) -+  (dst port, prio) -+  (src port, prio) -+   (dst port, prio) -+
 *       |          |         |          |         |          |         |           |
 *       |exceeded  |         |exceeded  |         |exceeded  |         |exceeded   |
 *       v          |         v          |         v          |         v           |
 *  BUF_P_RSRV_I  ok|    BUF_P_RSRV_E  ok|    REF_P_RSRV_I  ok|    REF_P_RSRV_E   ok|
 *   (src port) ----+     (dst port) ----+     (src port) ----+     (dst port) -----+
 *       |          |         |          |         |          |         |           |
 *       |exceeded  |         |exceeded  |         |exceeded  |         |exceeded   |
 *       v          |         v          |         v          |         v           |
 * BUF_PRIO_SHR_I ok|   BUF_PRIO_SHR_E ok|   REF_PRIO_SHR_I ok|   REF_PRIO_SHR_E  ok|
 *     (prio) ------+       (prio) ------+       (prio) ------+       (prio) -------+
 *       |          |         |          |         |          |         |           |
 *       |exceeded  |         |exceeded  |         |exceeded  |         |exceeded   |
 *       v          |         v          |         v          |         v           |
 * BUF_COL_SHR_I  ok|   BUF_COL_SHR_E  ok|   REF_COL_SHR_I  ok|   REF_COL_SHR_E   ok|
 *      (dp) -------+        (dp) -------+        (dp) -------+        (dp) --------+
 *       |          |         |          |         |          |         |           |
 *       |exceeded  |         |exceeded  |         |exceeded  |         |exceeded   |
 *       v          v         v          v         v          v         v           v
 *      fail     success     fail     success     fail     success     fail      success
 *       |          |         |          |         |          |         |           |
 *       v          v         v          v         v          v         v           v
 *       +-----+----+         +-----+----+         +-----+----+         +-----+-----+
 *             |                    |                    |                    |
 *             +-------> OR <-------+                    +-------> OR <-------+
 *                        |                                        |
 *                        v                                        v
 *                        +----------------> AND <-----------------+
 *                                            |
 *                                            v
 *                                    FIFO drop / accept
 *
 * We are modeling each of the 4 parallel lookups as a devlink-sb pool.
 * At least one (ingress or egress) memory pool and one (ingress or egress)
 * frame reference pool need to have resources for frame acceptance to succeed.
 *
 * The following watermarks are controlled explicitly through devlink-sb:
 * BUF_Q_RSRV_I, BUF_Q_RSRV_E, REF_Q_RSRV_I, REF_Q_RSRV_E
 * BUF_P_RSRV_I, BUF_P_RSRV_E, REF_P_RSRV_I, REF_P_RSRV_E
 * The following watermarks are controlled implicitly through devlink-sb:
 * BUF_COL_SHR_I, BUF_COL_SHR_E, REF_COL_SHR_I, REF_COL_SHR_E
 * The following watermarks are unused and disabled:
 * BUF_PRIO_SHR_I, BUF_PRIO_SHR_E, REF_PRIO_SHR_I, REF_PRIO_SHR_E
 *
 * This function overrides the hardware defaults with more sane ones (no
 * reservations by default, let sharing use all resources) and disables the
 * unused watermarks.
 */
void ocelot_watermark_init(struct ocelot *ocelot)
{
	int all_tcs = GENMASK(OCELOT_NUM_TC - 1, 0);
	int port;

	ocelot_write(ocelot, all_tcs, QSYS_RES_QOS_MODE);

	for (port = 0; port <= ocelot->num_phys_ports; port++)
		ocelot_disable_reservation_watermarks(ocelot, port);

	ocelot_disable_tc_sharing_watermarks(ocelot);
	ocelot_setup_sharing_watermarks(ocelot);
}
