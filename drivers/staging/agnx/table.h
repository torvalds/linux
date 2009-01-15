#ifndef AGNX_TABLE_H_
#define AGNX_TABLE_H_

void tx_fir_table_init(struct agnx_priv *priv);
void gain_table_init(struct agnx_priv *priv);
void monitor_gain_table_init(struct agnx_priv *priv);
void routing_table_init(struct agnx_priv *priv);
void tx_engine_lookup_tbl_init(struct agnx_priv *priv);

#endif /* AGNX_TABLE_H_ */
