#ifndef __BACKPORT_PCMCIA_DEVICE_ID_H
#define __BACKPORT_PCMCIA_DEVICE_ID_H
#include_next <pcmcia/device_id.h>

#ifndef PCMCIA_DEVICE_MANF_CARD_PROD_ID3
#define PCMCIA_DEVICE_MANF_CARD_PROD_ID3(manf, card, v3, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3, \
	.manf_id = (manf), \
	.card_id = (card), \
	.prod_id = { NULL, NULL, (v3), NULL }, \
	.prod_id_hash = { 0, 0, (vh3), 0 }, }
#endif

#ifndef PCMCIA_DEVICE_PROD_ID3
#define PCMCIA_DEVICE_PROD_ID3(v3, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID3, \
	.prod_id = { NULL, NULL, (v3), NULL },  \
	.prod_id_hash = { 0, 0, (vh3), 0 }, }
#endif

#endif /* __BACKPORT_PCMCIA_DEVICE_ID_H */
