/**
  * This header file contains data structures and
  * function declarations of 802.11d
  */
#ifndef _WLAN_11D_
#define _WLAN_11D_

#include "types.h"
#include "defs.h"

#define UNIVERSAL_REGION_CODE			0xff

/** (Beaconsize(256)-5(IEId,len,contrystr(3))/3(FirstChan,NoOfChan,MaxPwr)
 */
#define MRVDRV_MAX_SUBBAND_802_11D		83

#define COUNTRY_CODE_LEN			3
#define MAX_NO_OF_CHAN 				40

struct cmd_ds_command;

/** Data structure for Country IE*/
struct ieeetypes_subbandset {
	u8 firstchan;
	u8 nrchan;
	u8 maxtxpwr;
} __attribute__ ((packed));

struct ieeetypes_countryinfoset {
	u8 element_id;
	u8 len;
	u8 countrycode[COUNTRY_CODE_LEN];
	struct ieeetypes_subbandset subband[1];
};

struct ieeetypes_countryinfofullset {
	u8 element_id;
	u8 len;
	u8 countrycode[COUNTRY_CODE_LEN];
	struct ieeetypes_subbandset subband[MRVDRV_MAX_SUBBAND_802_11D];
} __attribute__ ((packed));

struct mrvlietypes_domainparamset {
	struct mrvlietypesheader header;
	u8 countrycode[COUNTRY_CODE_LEN];
	struct ieeetypes_subbandset subband[1];
} __attribute__ ((packed));

struct cmd_ds_802_11d_domain_info {
	u16 action;
	struct mrvlietypes_domainparamset domain;
} __attribute__ ((packed));

/** domain regulatory information */
struct wlan_802_11d_domain_reg {
	/** country Code*/
	u8 countrycode[COUNTRY_CODE_LEN];
	/** No. of subband*/
	u8 nr_subband;
	struct ieeetypes_subbandset subband[MRVDRV_MAX_SUBBAND_802_11D];
};

struct chan_power_11d {
	u8 chan;
	u8 pwr;
} __attribute__ ((packed));

struct parsed_region_chan_11d {
	u8 band;
	u8 region;
	s8 countrycode[COUNTRY_CODE_LEN];
	struct chan_power_11d chanpwr[MAX_NO_OF_CHAN];
	u8 nr_chan;
} __attribute__ ((packed));

struct region_code_mapping {
	u8 region[COUNTRY_CODE_LEN];
	u8 code;
};

u8 libertas_get_scan_type_11d(u8 chan,
			  struct parsed_region_chan_11d *parsed_region_chan);

u32 libertas_chan_2_freq(u8 chan, u8 band);

enum state_11d libertas_get_state_11d(wlan_private * priv);

void libertas_init_11d(wlan_private * priv);

int libertas_set_universaltable(wlan_private * priv, u8 band);

int libertas_cmd_802_11d_domain_info(wlan_private * priv,
				 struct cmd_ds_command *cmd, u16 cmdno,
				 u16 cmdOption);

int libertas_cmd_enable_11d(wlan_private * priv, struct iwreq *wrq);

int libertas_ret_802_11d_domain_info(wlan_private * priv,
				 struct cmd_ds_command *resp);

int libertas_parse_dnld_countryinfo_11d(wlan_private * priv);

int libertas_create_dnld_countryinfo_11d(wlan_private * priv);

#endif				/* _WLAN_11D_ */
