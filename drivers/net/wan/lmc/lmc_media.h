#ifndef _LMC_MEDIA_H_
#define _LMC_MEDIA_H_

lmc_media_t lmc_ds3_media = {
  lmc_ds3_init,			/* special media init stuff */
  lmc_ds3_default,		/* reset to default state */
  lmc_ds3_set_status,		/* reset status to state provided */
  lmc_dummy_set_1,		/* set clock source */
  lmc_dummy_set2_1,		/* set line speed */
  lmc_ds3_set_100ft,		/* set cable length */
  lmc_ds3_set_scram,		/* set scrambler */
  lmc_ds3_get_link_status,	/* get link status */
  lmc_dummy_set_1,		/* set link status */
  lmc_ds3_set_crc_length,	/* set CRC length */
  lmc_dummy_set_1,		/* set T1 or E1 circuit type */
  lmc_ds3_watchdog
};

lmc_media_t lmc_hssi_media = {
  lmc_hssi_init,		/* special media init stuff */
  lmc_hssi_default,		/* reset to default state */
  lmc_hssi_set_status,		/* reset status to state provided */
  lmc_hssi_set_clock,		/* set clock source */
  lmc_dummy_set2_1,		/* set line speed */
  lmc_dummy_set_1,		/* set cable length */
  lmc_dummy_set_1,		/* set scrambler */
  lmc_hssi_get_link_status,	/* get link status */
  lmc_hssi_set_link_status,	/* set link status */
  lmc_hssi_set_crc_length,	/* set CRC length */
  lmc_dummy_set_1,		/* set T1 or E1 circuit type */
  lmc_hssi_watchdog
};

lmc_media_t lmc_ssi_media = { lmc_ssi_init,	/* special media init stuff */
  lmc_ssi_default,		/* reset to default state */
  lmc_ssi_set_status,		/* reset status to state provided */
  lmc_ssi_set_clock,		/* set clock source */
  lmc_ssi_set_speed,		/* set line speed */
  lmc_dummy_set_1,		/* set cable length */
  lmc_dummy_set_1,		/* set scrambler */
  lmc_ssi_get_link_status,	/* get link status */
  lmc_ssi_set_link_status,	/* set link status */
  lmc_ssi_set_crc_length,	/* set CRC length */
  lmc_dummy_set_1,		/* set T1 or E1 circuit type */
  lmc_ssi_watchdog
};

lmc_media_t lmc_t1_media = {
  lmc_t1_init,			/* special media init stuff */
  lmc_t1_default,		/* reset to default state */
  lmc_t1_set_status,		/* reset status to state provided */
  lmc_t1_set_clock,		/* set clock source */
  lmc_dummy_set2_1,		/* set line speed */
  lmc_dummy_set_1,		/* set cable length */
  lmc_dummy_set_1,		/* set scrambler */
  lmc_t1_get_link_status,	/* get link status */
  lmc_dummy_set_1,		/* set link status */
  lmc_t1_set_crc_length,	/* set CRC length */
  lmc_t1_set_circuit_type,	/* set T1 or E1 circuit type */
  lmc_t1_watchdog
};


#endif

