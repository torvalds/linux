#include <linux/ethtool.h>

#include "fbnic_csr.h"

struct fbnic_stat_counter {
	u64 value;
	union {
		u32 old_reg_value_32;
		u64 old_reg_value_64;
	} u;
	bool reported;
};

struct fbnic_eth_mac_stats {
	struct fbnic_stat_counter FramesTransmittedOK;
	struct fbnic_stat_counter FramesReceivedOK;
	struct fbnic_stat_counter FrameCheckSequenceErrors;
	struct fbnic_stat_counter AlignmentErrors;
	struct fbnic_stat_counter OctetsTransmittedOK;
	struct fbnic_stat_counter FramesLostDueToIntMACXmitError;
	struct fbnic_stat_counter OctetsReceivedOK;
	struct fbnic_stat_counter FramesLostDueToIntMACRcvError;
	struct fbnic_stat_counter MulticastFramesXmittedOK;
	struct fbnic_stat_counter BroadcastFramesXmittedOK;
	struct fbnic_stat_counter MulticastFramesReceivedOK;
	struct fbnic_stat_counter BroadcastFramesReceivedOK;
	struct fbnic_stat_counter FrameTooLongErrors;
};

struct fbnic_mac_stats {
	struct fbnic_eth_mac_stats eth_mac;
};

struct fbnic_hw_stats {
	struct fbnic_mac_stats mac;
};

u64 fbnic_stat_rd64(struct fbnic_dev *fbd, u32 reg, u32 offset);

void fbnic_get_hw_stats(struct fbnic_dev *fbd);
