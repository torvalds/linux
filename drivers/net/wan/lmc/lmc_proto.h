#ifndef _LMC_PROTO_H_
#define _LMC_PROTO_H_

void lmc_proto_init(lmc_softc_t *sc);
void lmc_proto_attach(lmc_softc_t *sc);
void lmc_proto_detach(lmc_softc_t *sc);
void lmc_proto_reopen(lmc_softc_t *sc);
int lmc_proto_ioctl(lmc_softc_t *sc, struct ifreq *ifr, int cmd);
void lmc_proto_open(lmc_softc_t *sc);
void lmc_proto_close(lmc_softc_t *sc);
unsigned short lmc_proto_type(lmc_softc_t *sc, struct sk_buff *skb);
void lmc_proto_netif(lmc_softc_t *sc, struct sk_buff *skb);
int lmc_skb_rawpackets(char *buf, char **start, off_t offset, int len, int unused);

#endif

