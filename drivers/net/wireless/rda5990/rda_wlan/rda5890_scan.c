#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ieee80211.h>
#include <net/iw_handler.h>
#include <linux/etherdevice.h>


#include "rda5890_dev.h"
#include "rda5890_defs.h"

//#define SCAN_RESULT_DEBUG

//added by xiongzhi for wapi
#ifndef FCS_LEN
#define FCS_LEN                 4
#endif

/* Element ID  of various Information Elements */
typedef enum {ISSID         = 0,   /* Service Set Identifier   */
              ISUPRATES     = 1,   /* Supported Rates          */
              IFHPARMS      = 2,   /* FH parameter set         */
              IDSPARMS      = 3,   /* DS parameter set         */
              ICFPARMS      = 4,   /* CF parameter set         */
              ITIM          = 5,   /* Traffic Information Map  */
              IIBPARMS      = 6,   /* IBSS parameter set       */
              ICTEXT        = 16,  /* Challenge Text           */
              IERPINFO      = 42,  /* ERP Information          */
              IEXSUPRATES   = 50,   /* Extended Supported Rates */            
              IWAPI               =68          
} ELEMENTID_T;

/* Capability Information field bit assignments  */
typedef enum {ESS           = 0x01,   /* ESS capability               */
              IBSS          = 0x02,   /* IBSS mode                    */
              POLLABLE      = 0x04,   /* CF Pollable                  */
              POLLREQ       = 0x08,   /* Request to be polled         */
              PRIVACY       = 0x10,   /* WEP encryption supported     */
              SHORTPREAMBLE  = 0x20,   /* Short Preamble is supported  */
              SHORTSLOT      = 0x400,  /* Short Slot is supported      */
              PBCC          = 0x40,   /* PBCC                         */
              CHANNELAGILITY = 0x80,   /* Channel Agility              */
              SPECTRUM_MGMT = 0x100,  /* Spectrum Management          */
              DSSS_OFDM      = 0x2000  /* DSSS-OFDM                    */
} CAPABILITY_T;

/* BSS type */
typedef enum {INFRASTRUCTURE  = 1,
              INDEPENDENT     = 2,
              ANY_BSS         = 3
} BSSTYPE_T;

unsigned char* get_ie_elem(unsigned char* msa, ELEMENTID_T elm_id,
                                            unsigned short rx_len,unsigned short tag_param_offset)
{
    unsigned short index = 0;

    /*************************************************************************/
    /*                       Beacon Frame - Frame Body                       */
    /* --------------------------------------------------------------------- */
    /* |Timestamp |BeaconInt |CapInfo |SSID |SupRates |DSParSet |TIM elm   | */
    /* --------------------------------------------------------------------- */
    /* |8         |2         |2       |2-34 |3-10     |3        |4-256     | */
    /* --------------------------------------------------------------------- */
    /*                                                                       */
    /*************************************************************************/

    index = tag_param_offset;

    /* Search for the TIM Element Field and return if the element is found */
    while(index < (rx_len - FCS_LEN))
    {
        if(msa[index] == elm_id)
        {
            return(&msa[index]);
        }
        else
        {
            index += (2 + msa[index + 1]);
        }
    }

    return(0);
}

/* This function extracts the 'from ds' bit from the MAC header of the input */
/* frame.                                                                    */
/* Returns the value in the LSB of the returned value.                       */
unsigned char get_from_ds(unsigned char* header)
{
    return ((header[1] & 0x02) >> 1);
}

/* This function extracts the 'to ds' bit from the MAC header of the input   */
/* frame.                                                                    */
/* Returns the value in the LSB of the returned value.                       */
unsigned char get_to_ds(unsigned char* header)
{
    return (header[1] & 0x01);
}

/* This function extracts the MAC Address in 'address1' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
void get_address1(unsigned char* msa, unsigned char* addr)
{
    memcpy(addr, msa + 4, 6);
}

/* This function extracts the MAC Address in 'address2' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
void get_address2(unsigned char* msa, unsigned char* addr)
{
    memcpy(addr, msa + 10, 6);
}

/* This function extracts the MAC Address in 'address3' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
void get_address3(unsigned char* msa, unsigned char* addr)
{
    memcpy(addr, msa + 16, 6);
}

/* This function extracts the BSSID from the incoming WLAN packet based on   */
/* the 'from ds' bit, and updates the MAC Address in the allocated 'addr'    */
/* variable.                                                                 */
void get_BSSID(unsigned char* data, unsigned char* bssid)
{
    if(get_from_ds(data) == 1)
        get_address2(data, bssid);
    else if(get_to_ds(data) == 1)
        get_address1(data, bssid);
    else
        get_address3(data, bssid);
}

extern int is_same_network(struct bss_descriptor *src,
                  struct bss_descriptor *dst);
extern void clear_bss_descriptor(struct bss_descriptor *bss);
void rda5890_network_information(struct rda5890_private *priv, 
		char *info, unsigned short info_len)
{
    union iwreq_data wrqu;
    struct bss_descriptor *iter_bss;
    struct bss_descriptor *safe;
    struct bss_descriptor bss_desc;
    struct bss_descriptor * bss = &bss_desc;

    unsigned char  *pos, *end, *p;
    unsigned char n_ex_rates = 0, got_basic_rates = 0, n_basic_rates = 0;
    struct ieee_ie_country_info_set *pcountryinfo;
    int ret;
    unsigned char* msa = &info[9];
    unsigned short msa_len = info[6] | (info[7] << 8);

    if(!priv->scan_running)
        goto done;
    
    if((msa_len - 1 + 9 ) != info_len)
        {
            RDA5890_ERRP("rda5890_network_information verify lengh feild failed \n");
        }

    memset(bss, 0, sizeof (struct bss_descriptor));
    bss->rssi = info[8];
    msa_len -= 1; // has rssi

    get_BSSID(msa, bss->bssid);

    end = msa + msa_len;
    
    //mac head
    pos = msa + 24;
    //time stamp
    pos += 8;
    //beacon
    bss->beaconperiod = *(pos) | (*(pos + 1) << 8);
    pos += 2 ;
    //capability
    bss->capability = *(pos) | (*(pos + 1) << 8);
    pos += 2;

    if (bss->capability & WLAN_CAPABILITY_IBSS)
        bss->mode = IW_MODE_ADHOC;
    else
        bss->mode = IW_MODE_INFRA;

  /* process variable IE */
	while (pos + 2 <= end) {
		if (pos + pos[1] > end) {
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("process_bss: error in processing IE, "
				     "bytes left < IE length\n");
#endif
			break;
		}

		switch (pos[0]) {
		case WLAN_EID_SSID:
			bss->ssid_len = min_t(int, IEEE80211_MAX_SSID_LEN, pos[1]);
			memcpy(bss->ssid, pos + 2, bss->ssid_len);
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got SSID IE: '%s', len %u %d\n",
			             bss->ssid,
			             bss->ssid_len, pos[1]);
#endif
			break;

		case WLAN_EID_SUPP_RATES:
			n_basic_rates = min_t(uint8_t, MAX_RATES, pos[1]);
			memcpy(bss->rates, pos + 2, n_basic_rates);
			got_basic_rates = 1;
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got RATES IE\n");
#endif
			break;

		case WLAN_EID_FH_PARAMS:
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got FH IE\n");
#endif
			break;

		case WLAN_EID_DS_PARAMS:
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got DS IE\n");
#endif
			break;

		case WLAN_EID_CF_PARAMS:
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got CF IE\n");
#endif
			break;

		case WLAN_EID_IBSS_PARAMS:
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got IBSS IE\n");
#endif
			break;

		case WLAN_EID_COUNTRY:
			pcountryinfo = (struct ieee_ie_country_info_set *) pos;
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got COUNTRY IE\n");
#endif
			break;

		case WLAN_EID_EXT_SUPP_RATES:
			/* only process extended supported rate if data rate is
			 * already found. Data rate IE should come before
			 * extended supported rate IE
			 */
#ifdef SCAN_RESULT_DEBUG			 
			RDA5890_DBGP("got RATESEX IE\n");
#endif
			if (!got_basic_rates) {
#ifdef SCAN_RESULT_DEBUG                
				RDA5890_DBGP("... but ignoring it\n");
#endif
				break;
			}

			n_ex_rates = pos[1];
			if (n_basic_rates + n_ex_rates > MAX_RATES)
				n_ex_rates = MAX_RATES - n_basic_rates;

			p = bss->rates + n_basic_rates;
			memcpy(p, pos + 2, n_ex_rates);
			break;

		case WLAN_EID_GENERIC:
			if (pos[1] >= 4 &&
			    pos[2] == 0x00 && pos[3] == 0x50 &&
			    pos[4] == 0xf2 && pos[5] == 0x01) {
				bss->wpa_ie_len = min(pos[1] + 2, MAX_WPA_IE_LEN);
				memcpy(bss->wpa_ie, pos, bss->wpa_ie_len);
#ifdef SCAN_RESULT_DEBUG                
				RDA5890_DBGP("got WPA IE \n");
#endif
			}
			else {
#ifdef SCAN_RESULT_DEBUG                
				RDA5890_DBGP("got generic IE: %02x:%02x:%02x:%02x, len %d\n",
					pos[2], pos[3],
					pos[4], pos[5],
					pos[1]);
#endif
			}
			break;

		case WLAN_EID_RSN:
#ifdef SCAN_RESULT_DEBUG            
			RDA5890_DBGP("got RSN IE\n");
#endif
			bss->rsn_ie_len = min(pos[1] + 2, MAX_WPA_IE_LEN);
			memcpy(bss->rsn_ie, pos, bss->rsn_ie_len);
			break;

            case IWAPI:
#ifdef SCAN_RESULT_DEBUG                
                    RDA5890_DBGP("got WAPI IE\n");
#endif
            bss->wapi_ie_len = min(pos[1] + 2, 100);
            memcpy(bss->wapi_ie, pos, bss->wapi_ie_len);
            break;

    		default:
    		break;
		}

		pos += pos[1] + 2;
	}
    
      bss->last_scanned = jiffies;

    /* add scaned bss into list */
	if (1) {
        		struct bss_descriptor *found = NULL;
        		struct bss_descriptor *oldest = NULL;

        		/* Try to find this bss in the scan table */
        		list_for_each_entry (iter_bss, &priv->network_list, list) {
			if (is_same_network(iter_bss, bss)) {
				found = iter_bss;
				break;
			}

			if ((oldest == NULL) ||
			    (iter_bss->last_scanned < oldest->last_scanned))
				oldest = iter_bss;
		}

		if (found) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND SAME %s, update\n", found->ssid);
			/* found, clear it */
			clear_bss_descriptor(found);
		} else if (!list_empty(&priv->network_free_list)) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND NEW %s, add\n", bss->ssid);
			/* Pull one from the free list */
			found = list_entry(priv->network_free_list.next,
					   struct bss_descriptor, list);
			list_move_tail(&found->list, &priv->network_list);
		} else if (oldest) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND NEW %s, no space, replace oldest %s\n", 
				bss->ssid, oldest->ssid);
			/* If there are no more slots, expire the oldest */
			found = oldest;
			clear_bss_descriptor(found);
			list_move_tail(&found->list, &priv->network_list);
		} else {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND NEW but no space to store\n");
		}

		/* Copy the locally created newbssentry to the scan table */
		memcpy(found, bss, offsetof(struct bss_descriptor, list));
	}

done:
#ifdef SCAN_RESULT_DEBUG    
	RDA5890_DBGP("rda5890_network_information ret %d \n", ret);
#endif
    return;
}



