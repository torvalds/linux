/*
 * Model description tables
 */

typedef struct product_info_t {
	const char     *pi_name;
	const char     *pi_type;
} product_info_t;

typedef struct vendor_info_t {
	const char     *vi_name;
	const product_info_t *vi_product_info;
} vendor_info_t;

/*
 * Base models
 */
static const char * const txt_base_models[] = {
  "MQ 2", "MQ Pro", "SP 25", "SP 50", "SP 100", "SP 5000", "SP 7000", "SP 1000", "Unknown"
};
#define N_BASE_MODELS (sizeof(txt_base_models)/sizeof(char*)-1)

/*
 * Eicon Networks
 */
static const char txt_en_mq[] = "Masquerade";
static const char txt_en_sp[] = "Safepipe";

static const product_info_t product_info_eicon[] = {
  { txt_en_mq, "II"   }, /*  0 */
  { txt_en_mq, "Pro"  }, /*  1 */
  { txt_en_sp, "25"   }, /*  2 */
  { txt_en_sp, "50"   }, /*  3 */
  { txt_en_sp, "100"  }, /*  4 */
  { txt_en_sp, "5000" }, /*  5 */
  { txt_en_sp, "7000" }, /*  6 */
  { txt_en_sp, "30"   }, /*  7 */
  { txt_en_sp, "5100" }, /*  8 */
  { txt_en_sp, "7100" }, /*  9 */
  { txt_en_sp, "1110" }, /* 10 */
  { txt_en_sp, "3020" }, /* 11 */
  { txt_en_sp, "3030" }, /* 12 */
  { txt_en_sp, "5020" }, /* 13 */
  { txt_en_sp, "5030" }, /* 14 */
  { txt_en_sp, "1120" }, /* 15 */
  { txt_en_sp, "1130" }, /* 16 */
  { txt_en_sp, "6010" }, /* 17 */
  { txt_en_sp, "6110" }, /* 18 */
  { txt_en_sp, "6210" }, /* 19 */
  { txt_en_sp, "1020" }, /* 20 */
  { txt_en_sp, "1040" }, /* 21 */
  { txt_en_sp, "1050" }, /* 22 */
  { txt_en_sp, "1060" }, /* 23 */
};
#define N_PRIDS (sizeof(product_info_eicon)/sizeof(product_info_t))

/*
 * The vendor table
 */
static vendor_info_t const vendor_info_table[] = {
  { "Eicon Networks",	product_info_eicon   },
};
#define N_VENDORS (sizeof(vendor_info_table)/sizeof(vendor_info_t))
