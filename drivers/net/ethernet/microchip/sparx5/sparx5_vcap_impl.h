/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver VCAP implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */

#ifndef __SPARX5_VCAP_IMPL_H__
#define __SPARX5_VCAP_IMPL_H__

#define SPARX5_VCAP_CID_IS2_L0 VCAP_CID_INGRESS_STAGE2_L0 /* IS2 lookup 0 */
#define SPARX5_VCAP_CID_IS2_L1 VCAP_CID_INGRESS_STAGE2_L1 /* IS2 lookup 1 */
#define SPARX5_VCAP_CID_IS2_L2 VCAP_CID_INGRESS_STAGE2_L2 /* IS2 lookup 2 */
#define SPARX5_VCAP_CID_IS2_L3 VCAP_CID_INGRESS_STAGE2_L3 /* IS2 lookup 3 */
#define SPARX5_VCAP_CID_IS2_MAX \
	(VCAP_CID_INGRESS_STAGE2_L3 + VCAP_CID_LOOKUP_SIZE - 1) /* IS2 Max */

#endif /* __SPARX5_VCAP_IMPL_H__ */
