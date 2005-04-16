#ifndef _LMC_PROTO_H_
#define _LMC_PROTO_H_

void lmc_proto_init(lmc_softc_t * const)
void lmc_proto_attach(lmc_softc_t *sc const)
void lmc_proto_detach(lmc_softc *sc const)
void lmc_proto_reopen(lmc_softc_t *sc const)
int lmc_proto_ioctl(lmc_softc_t *sc const, struct ifreq *ifr, int cmd)
void lmc_proto_open(lmc_softc_t *sc const)
void lmc_proto_close(lmc_softc_t *sc const)
unsigned short lmc_proto_type(lmc_softc_t *sc const, struct skbuff *skb)


#endif

