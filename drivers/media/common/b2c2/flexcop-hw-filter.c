// SPDX-License-Identifier: GPL-2.0
/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop-hw-filter.c - pid and mac address filtering and control functions
 * see flexcop.c for copyright information
 */
#include "flexcop.h"

static void flexcop_rcv_data_ctrl(struct flexcop_device *fc, int onoff)
{
	flexcop_set_ibi_value(ctrl_208, Rcv_Data_sig, onoff);
	deb_ts("rcv_data is now: '%s'\n", onoff ? "on" : "off");
}

void flexcop_smc_ctrl(struct flexcop_device *fc, int onoff)
{
	flexcop_set_ibi_value(ctrl_208, SMC_Enable_sig, onoff);
}

static void flexcop_null_filter_ctrl(struct flexcop_device *fc, int onoff)
{
	flexcop_set_ibi_value(ctrl_208, Null_filter_sig, onoff);
}

void flexcop_set_mac_filter(struct flexcop_device *fc, u8 mac[6])
{
	flexcop_ibi_value v418, v41c;
	v41c = fc->read_ibi_reg(fc, mac_address_41c);

	v418.mac_address_418.MAC1 = mac[0];
	v418.mac_address_418.MAC2 = mac[1];
	v418.mac_address_418.MAC3 = mac[2];
	v418.mac_address_418.MAC6 = mac[3];
	v41c.mac_address_41c.MAC7 = mac[4];
	v41c.mac_address_41c.MAC8 = mac[5];

	fc->write_ibi_reg(fc, mac_address_418, v418);
	fc->write_ibi_reg(fc, mac_address_41c, v41c);
}

void flexcop_mac_filter_ctrl(struct flexcop_device *fc, int onoff)
{
	flexcop_set_ibi_value(ctrl_208, MAC_filter_Mode_sig, onoff);
}

static void flexcop_pid_group_filter(struct flexcop_device *fc,
		u16 pid, u16 mask)
{
	/* index_reg_310.extra_index_reg need to 0 or 7 to work */
	flexcop_ibi_value v30c;
	v30c.pid_filter_30c_ext_ind_0_7.Group_PID = pid;
	v30c.pid_filter_30c_ext_ind_0_7.Group_mask = mask;
	fc->write_ibi_reg(fc, pid_filter_30c, v30c);
}

static void flexcop_pid_group_filter_ctrl(struct flexcop_device *fc, int onoff)
{
	flexcop_set_ibi_value(ctrl_208, Mask_filter_sig, onoff);
}

/* this fancy define reduces the code size of the quite similar PID controlling of
 * the first 6 PIDs
 */

#define pid_ctrl(vregname,field,enablefield,trans_field,transval) \
	flexcop_ibi_value vpid = fc->read_ibi_reg(fc, vregname), \
v208 = fc->read_ibi_reg(fc, ctrl_208); \
vpid.vregname.field = onoff ? pid : 0x1fff; \
vpid.vregname.trans_field = transval; \
v208.ctrl_208.enablefield = onoff; \
fc->write_ibi_reg(fc, vregname, vpid); \
fc->write_ibi_reg(fc, ctrl_208, v208);

static void flexcop_pid_Stream1_PID_ctrl(struct flexcop_device *fc,
		u16 pid, int onoff)
{
	pid_ctrl(pid_filter_300, Stream1_PID, Stream1_filter_sig,
			Stream1_trans, 0);
}

static void flexcop_pid_Stream2_PID_ctrl(struct flexcop_device *fc,
		u16 pid, int onoff)
{
	pid_ctrl(pid_filter_300, Stream2_PID, Stream2_filter_sig,
			Stream2_trans, 0);
}

static void flexcop_pid_PCR_PID_ctrl(struct flexcop_device *fc,
		u16 pid, int onoff)
{
	pid_ctrl(pid_filter_304, PCR_PID, PCR_filter_sig, PCR_trans, 0);
}

static void flexcop_pid_PMT_PID_ctrl(struct flexcop_device *fc,
		u16 pid, int onoff)
{
	pid_ctrl(pid_filter_304, PMT_PID, PMT_filter_sig, PMT_trans, 0);
}

static void flexcop_pid_EMM_PID_ctrl(struct flexcop_device *fc,
		u16 pid, int onoff)
{
	pid_ctrl(pid_filter_308, EMM_PID, EMM_filter_sig, EMM_trans, 0);
}

static void flexcop_pid_ECM_PID_ctrl(struct flexcop_device *fc,
		u16 pid, int onoff)
{
	pid_ctrl(pid_filter_308, ECM_PID, ECM_filter_sig, ECM_trans, 0);
}

static void flexcop_pid_control(struct flexcop_device *fc,
		int index, u16 pid, int onoff)
{
	if (pid == 0x2000)
		return;

	deb_ts("setting pid: %5d %04x at index %d '%s'\n",
			pid, pid, index, onoff ? "on" : "off");

	/* First 6 can be buggy - skip over them if option set */
	if (fc->skip_6_hw_pid_filter)
		index += 6;

	/* We could use bit magic here to reduce source code size.
	 * I decided against it, but to use the real register names */
	switch (index) {
	case 0:
		flexcop_pid_Stream1_PID_ctrl(fc, pid, onoff);
		break;
	case 1:
		flexcop_pid_Stream2_PID_ctrl(fc, pid, onoff);
		break;
	case 2:
		flexcop_pid_PCR_PID_ctrl(fc, pid, onoff);
		break;
	case 3:
		flexcop_pid_PMT_PID_ctrl(fc, pid, onoff);
		break;
	case 4:
		flexcop_pid_EMM_PID_ctrl(fc, pid, onoff);
		break;
	case 5:
		flexcop_pid_ECM_PID_ctrl(fc, pid, onoff);
		break;
	default:
		if (fc->has_32_hw_pid_filter && index < 38) {
			flexcop_ibi_value vpid, vid;

			/* set the index */
			vid = fc->read_ibi_reg(fc, index_reg_310);
			vid.index_reg_310.index_reg = index - 6;
			fc->write_ibi_reg(fc, index_reg_310, vid);

			vpid = fc->read_ibi_reg(fc, pid_n_reg_314);
			vpid.pid_n_reg_314.PID = onoff ? pid : 0x1fff;
			vpid.pid_n_reg_314.PID_enable_bit = onoff;
			fc->write_ibi_reg(fc, pid_n_reg_314, vpid);
		}
		break;
	}
}

static int flexcop_toggle_fullts_streaming(struct flexcop_device *fc, int onoff)
{
	if (fc->fullts_streaming_state != onoff) {
		deb_ts("%s full TS transfer\n",onoff ? "enabling" : "disabling");
		flexcop_pid_group_filter(fc, 0, 0x1fe0 * (!onoff));
		flexcop_pid_group_filter_ctrl(fc, onoff);
		fc->fullts_streaming_state = onoff;
	}
	return 0;
}

int flexcop_pid_feed_control(struct flexcop_device *fc,
		struct dvb_demux_feed *dvbdmxfeed, int onoff)
{
	int max_pid_filter = 6;

	max_pid_filter -= 6 * fc->skip_6_hw_pid_filter;
	max_pid_filter += 32 * fc->has_32_hw_pid_filter;

	fc->feedcount += onoff ? 1 : -1; /* the number of PIDs/Feed currently requested */
	if (dvbdmxfeed->index >= max_pid_filter)
		fc->extra_feedcount += onoff ? 1 : -1;

	/* toggle complete-TS-streaming when:
	 * - pid_filtering is not enabled and it is the first or last feed requested
	 * - pid_filtering is enabled,
	 *   - but the number of requested feeds is exceeded
	 *   - or the requested pid is 0x2000 */

	if (!fc->pid_filtering && fc->feedcount == onoff)
		flexcop_toggle_fullts_streaming(fc, onoff);

	if (fc->pid_filtering) {
		flexcop_pid_control \
			(fc, dvbdmxfeed->index, dvbdmxfeed->pid, onoff);

		if (fc->extra_feedcount > 0)
			flexcop_toggle_fullts_streaming(fc, 1);
		else if (dvbdmxfeed->pid == 0x2000)
			flexcop_toggle_fullts_streaming(fc, onoff);
		else
			flexcop_toggle_fullts_streaming(fc, 0);
	}

	/* if it was the first or last feed request change the stream-status */
	if (fc->feedcount == onoff) {
		flexcop_rcv_data_ctrl(fc, onoff);
		if (fc->stream_control) /* device specific stream control */
			fc->stream_control(fc, onoff);

		/* feeding stopped -> reset the flexcop filter*/
		if (onoff == 0) {
			flexcop_reset_block_300(fc);
			flexcop_hw_filter_init(fc);
		}
	}
	return 0;
}
EXPORT_SYMBOL(flexcop_pid_feed_control);

void flexcop_hw_filter_init(struct flexcop_device *fc)
{
	int i;
	flexcop_ibi_value v;
	int max_pid_filter = 6;

	max_pid_filter -= 6 * fc->skip_6_hw_pid_filter;
	max_pid_filter += 32 * fc->has_32_hw_pid_filter;

	for (i = 0; i < max_pid_filter; i++)
		flexcop_pid_control(fc, i, 0x1fff, 0);

	flexcop_pid_group_filter(fc, 0, 0x1fe0);
	flexcop_pid_group_filter_ctrl(fc, 0);

	v = fc->read_ibi_reg(fc, pid_filter_308);
	v.pid_filter_308.EMM_filter_4 = 1;
	v.pid_filter_308.EMM_filter_6 = 0;
	fc->write_ibi_reg(fc, pid_filter_308, v);

	flexcop_null_filter_ctrl(fc, 1);
}
