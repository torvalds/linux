/***********************************************************************
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

struct smsdvb_debugfs;
struct smsdvb_client_t;

typedef void (*sms_prt_dvb_stats_t)(struct smsdvb_debugfs *debug_data,
				    struct SMSHOSTLIB_STATISTICS_ST *p);

typedef void (*sms_prt_isdb_stats_t)(struct smsdvb_debugfs *debug_data,
				     struct SMSHOSTLIB_STATISTICS_ISDBT_ST *p);

typedef void (*sms_prt_isdb_stats_ex_t)
			(struct smsdvb_debugfs *debug_data,
			 struct SMSHOSTLIB_STATISTICS_ISDBT_EX_ST *p);


struct smsdvb_client_t {
	struct list_head entry;

	struct smscore_device_t *coredev;
	struct smscore_client_t *smsclient;

	struct dvb_adapter      adapter;
	struct dvb_demux        demux;
	struct dmxdev           dmxdev;
	struct dvb_frontend     frontend;

	fe_status_t             fe_status;

	struct completion       tune_done;
	struct completion       stats_done;

	int last_per;

	int legacy_ber, legacy_per;

	int event_fe_state;
	int event_unc_state;

	/* Stats debugfs data */
	struct dentry		*debugfs;

	struct smsdvb_debugfs	*debug_data;

	sms_prt_dvb_stats_t	prt_dvb_stats;
	sms_prt_isdb_stats_t	prt_isdb_stats;
	sms_prt_isdb_stats_ex_t	prt_isdb_stats_ex;
};

/*
 * This struct is a mix of RECEPTION_STATISTICS_EX_S and SRVM_SIGNAL_STATUS_S.
 * It was obtained by comparing the way it was filled by the original code
 */
struct RECEPTION_STATISTICS_PER_SLICES_S {
	u32 result;
	u32 snr;
	s32 inBandPower;
	u32 tsPackets;
	u32 etsPackets;
	u32 constellation;
	u32 hpCode;
	u32 tpsSrvIndLP;
	u32 tpsSrvIndHP;
	u32 cellId;
	u32 reason;
	u32 requestId;
	u32 ModemState;		/* from SMSHOSTLIB_DVB_MODEM_STATE_ET */

	u32 BER;		/* Post Viterbi BER [1E-5] */
	s32 RSSI;		/* dBm */
	s32 CarrierOffset;	/* Carrier Offset in bin/1024 */

	u32 IsRfLocked;		/* 0 - not locked, 1 - locked */
	u32 IsDemodLocked;	/* 0 - not locked, 1 - locked */

	u32 BERBitCount;	/* Total number of SYNC bits. */
	u32 BERErrorCount;	/* Number of erronous SYNC bits. */

	s32 MRC_SNR;		/* dB */
	s32 MRC_InBandPwr;	/* In band power in dBM */
	s32 MRC_RSSI;		/* dBm */
};

/* From smsdvb-debugfs.c */
#ifdef CONFIG_SMS_SIANO_DEBUGFS

int smsdvb_debugfs_create(struct smsdvb_client_t *client);
void smsdvb_debugfs_release(struct smsdvb_client_t *client);
int smsdvb_debugfs_register(void);
void smsdvb_debugfs_unregister(void);

#else

static inline int smsdvb_debugfs_create(struct smsdvb_client_t *client)
{
	return 0;
}

static inline void smsdvb_debugfs_release(struct smsdvb_client_t *client) {}

static inline int smsdvb_debugfs_register(void)
{
	return 0;
};

static inline void smsdvb_debugfs_unregister(void) {};

#endif

