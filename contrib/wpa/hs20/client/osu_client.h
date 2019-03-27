/*
 * Hotspot 2.0 - OSU client
 * Copyright (c) 2013-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef OSU_CLIENT_H
#define OSU_CLIENT_H

#define SPP_NS_URI "http://www.wi-fi.org/specifications/hotspot2dot0/v1.0/spp"

#define URN_OMA_DM_DEVINFO "urn:oma:mo:oma-dm-devinfo:1.0"
#define URN_OMA_DM_DEVDETAIL "urn:oma:mo:oma-dm-devdetail:1.0"
#define URN_HS20_DEVDETAIL_EXT "urn:wfa:mo-ext:hotspot2dot0-devdetail-ext:1.0"
#define URN_HS20_PPS "urn:wfa:mo:hotspot2dot0-perprovidersubscription:1.0"


#define MAX_OSU_VALS 10

struct osu_lang_text {
	char lang[4];
	char text[253];
};

struct hs20_osu_client {
	struct xml_node_ctx *xml;
	struct http_ctx *http;
	int no_reconnect;
	char pps_fname[300];
	char *devid;
	const char *result_file;
	const char *summary_file;
	const char *ifname;
	const char *ca_fname;
	int no_osu_cert_validation; /* for EST operations */
	char *fqdn;
	char *server_url;
	struct osu_lang_text friendly_name[MAX_OSU_VALS];
	size_t friendly_name_count;
	size_t icon_count;
	char icon_filename[MAX_OSU_VALS][256];
	u8 icon_hash[MAX_OSU_VALS][32];
	int pps_cred_set;
	int pps_updated;
	int client_cert_present;
	char **server_dnsname;
	size_t server_dnsname_count;
	const char *osu_ssid; /* Enforced OSU_SSID for testing purposes */
#define WORKAROUND_OCSP_OPTIONAL 0x00000001
	unsigned long int workarounds;
};


/* osu_client.c */

void write_result(struct hs20_osu_client *ctx, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
void write_summary(struct hs20_osu_client *ctx, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

void debug_dump_node(struct hs20_osu_client *ctx, const char *title,
		     xml_node_t *node);
int osu_get_certificate(struct hs20_osu_client *ctx, xml_node_t *getcert);
int hs20_add_pps_mo(struct hs20_osu_client *ctx, const char *uri,
		    xml_node_t *add_mo, char *fname, size_t fname_len);
void get_user_pw(struct hs20_osu_client *ctx, xml_node_t *pps,
		 const char *alt_loc, char **user, char **pw);
int update_pps_file(struct hs20_osu_client *ctx, const char *pps_fname,
		    xml_node_t *pps);
void cmd_set_pps(struct hs20_osu_client *ctx, const char *pps_fname);


/* spp_client.c */

void spp_sub_rem(struct hs20_osu_client *ctx, const char *address,
		 const char *pps_fname,
		 const char *client_cert, const char *client_key,
		 const char *cred_username, const char *cred_password,
		 xml_node_t *pps);
void spp_pol_upd(struct hs20_osu_client *ctx, const char *address,
		 const char *pps_fname,
		 const char *client_cert, const char *client_key,
		 const char *cred_username, const char *cred_password,
		 xml_node_t *pps);
int cmd_prov(struct hs20_osu_client *ctx, const char *url);
int cmd_sim_prov(struct hs20_osu_client *ctx, const char *url);


/* oma_dm_client.c */

int cmd_oma_dm_prov(struct hs20_osu_client *ctx, const char *url);
int cmd_oma_dm_sim_prov(struct hs20_osu_client *ctx, const char *url);
void oma_dm_sub_rem(struct hs20_osu_client *ctx, const char *address,
		    const char *pps_fname,
		    const char *client_cert, const char *client_key,
		    const char *cred_username, const char *cred_password,
		    xml_node_t *pps);
void oma_dm_pol_upd(struct hs20_osu_client *ctx, const char *address,
		    const char *pps_fname,
		    const char *client_cert, const char *client_key,
		    const char *cred_username, const char *cred_password,
		    xml_node_t *pps);
void cmd_oma_dm_sub_rem(struct hs20_osu_client *ctx, const char *address,
			const char *pps_fname);
void cmd_oma_dm_add(struct hs20_osu_client *ctx, const char *pps_fname,
		    const char *add_fname);
void cmd_oma_dm_replace(struct hs20_osu_client *ctx, const char *pps_fname,
			const char *replace_fname);

/* est.c */

int est_load_cacerts(struct hs20_osu_client *ctx, const char *url);
int est_build_csr(struct hs20_osu_client *ctx, const char *url);
int est_simple_enroll(struct hs20_osu_client *ctx, const char *url,
		      const char *user, const char *pw);

#endif /* OSU_CLIENT_H */
