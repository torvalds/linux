#ifndef _CREATE_FW_H_
#define _CREATE_FW_H_
#include "../probe_roms.h"


/* we are configuring for 2 SCUs */
static const int num_elements = 2;

/*
 * For all defined arrays:
 * elements 0-3 are for SCU0, ports 0-3
 * elements 4-7 are for SCU1, ports 0-3
 *
 * valid configurations for one SCU are:
 *  P0  P1  P2  P3
 * ----------------
 * 0xF,0x0,0x0,0x0 # 1 x4 port
 * 0x3,0x0,0x4,0x8 # Phys 0 and 1 are a x2 port, phy 2 and phy 3 are each x1
 *                 # ports
 * 0x1,0x2,0xC,0x0 # Phys 0 and 1 are each x1 ports, phy 2 and phy 3 are a x2
 *                 # port
 * 0x3,0x0,0xC,0x0 # Phys 0 and 1 are a x2 port, phy 2 and phy 3 are a x2 port
 * 0x1,0x2,0x4,0x8 # Each phy is a x1 port (this is the default configuration)
 *
 * if there is a port/phy on which you do not wish to override the default
 * values, use the value assigned to UNINIT_PARAM (255).
 */

/* discovery mode type (port auto config mode by default ) */

/*
 * if there is a port/phy on which you do not wish to override the default
 * values, use the value "0000000000000000". SAS address of zero's is
 * considered invalid and will not be used.
 */
#ifdef MPC
static const int mode_type = SCIC_PORT_MANUAL_CONFIGURATION_MODE;
static const __u8 phy_mask[2][4] = { {1, 2, 4, 8},
				     {1, 2, 4, 8} };
static const unsigned long long sas_addr[2][4] = { { 0x5FCFFFFFF0000001ULL,
						     0x5FCFFFFFF0000002ULL,
						     0x5FCFFFFFF0000003ULL,
						     0x5FCFFFFFF0000004ULL },
						   { 0x5FCFFFFFF0000005ULL,
						     0x5FCFFFFFF0000006ULL,
						     0x5FCFFFFFF0000007ULL,
						     0x5FCFFFFFF0000008ULL } };
#else	/* APC (default) */
static const int mode_type = SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE;
static const __u8 phy_mask[2][4];
static const unsigned long long sas_addr[2][4] = { { 0x5FCFFFFF00000001ULL,
						     0x5FCFFFFF00000001ULL,
						     0x5FCFFFFF00000001ULL,
						     0x5FCFFFFF00000001ULL },
						   { 0x5FCFFFFF00000002ULL,
						     0x5FCFFFFF00000002ULL,
						     0x5FCFFFFF00000002ULL,
						     0x5FCFFFFF00000002ULL } };
#endif

/* Maximum number of concurrent device spin up */
static const int max_num_concurrent_dev_spin_up = 1;

/* enable of ssc operation */
static const int enable_ssc;

/* AFE_TX_AMP_CONTROL */
static const unsigned int afe_tx_amp_control0 = 0x000e7c03;
static const unsigned int afe_tx_amp_control1 = 0x000e7c03;
static const unsigned int afe_tx_amp_control2 = 0x000e7c03;
static const unsigned int afe_tx_amp_control3 = 0x000e7c03;

static const char blob_name[] = "isci_firmware.bin";
static const char sig[] = "ISCUOEMB";
static const unsigned char version = 0x10;

#endif
