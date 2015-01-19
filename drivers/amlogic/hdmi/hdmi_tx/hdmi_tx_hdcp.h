#ifndef __HDMI_TX_HDCP_H
#define __HDMI_TX_HDCP_H
/*
    hdmi_tx_hdcp.c
    version 1.0
*/

// Notic: the HDCP key setting has been moved to uboot
// On MBX project, it is too late for HDCP get from
// other devices

//int task_tx_key_setting(unsigned force_wrong);

int hdcp_ksv_valid(unsigned char * dat);

#endif

