/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef __MACSEC_API_H__
#define __MACSEC_API_H__

#include "aq_hw.h"
#include "macsec_struct.h"

#define NUMROWS_INGRESSPRECTLFRECORD 24
#define ROWOFFSET_INGRESSPRECTLFRECORD 0

#define NUMROWS_INGRESSPRECLASSRECORD 48
#define ROWOFFSET_INGRESSPRECLASSRECORD 0

#define NUMROWS_INGRESSPOSTCLASSRECORD 48
#define ROWOFFSET_INGRESSPOSTCLASSRECORD 0

#define NUMROWS_INGRESSSCRECORD 32
#define ROWOFFSET_INGRESSSCRECORD 0

#define NUMROWS_INGRESSSARECORD 32
#define ROWOFFSET_INGRESSSARECORD 32

#define NUMROWS_INGRESSSAKEYRECORD 32
#define ROWOFFSET_INGRESSSAKEYRECORD 0

#define NUMROWS_INGRESSPOSTCTLFRECORD 24
#define ROWOFFSET_INGRESSPOSTCTLFRECORD 0

#define NUMROWS_EGRESSCTLFRECORD 24
#define ROWOFFSET_EGRESSCTLFRECORD 0

#define NUMROWS_EGRESSCLASSRECORD 48
#define ROWOFFSET_EGRESSCLASSRECORD 0

#define NUMROWS_EGRESSSCRECORD 32
#define ROWOFFSET_EGRESSSCRECORD 0

#define NUMROWS_EGRESSSARECORD 32
#define ROWOFFSET_EGRESSSARECORD 32

#define NUMROWS_EGRESSSAKEYRECORD 32
#define ROWOFFSET_EGRESSSAKEYRECORD 96

/*!  Read the raw table data from the specified row of the Egress CTL
 *   Filter table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 23).
 */
int aq_mss_get_egress_ctlf_record(struct aq_hw_s *hw,
				  struct aq_mss_egress_ctlf_record *rec,
				  u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Egress CTL Filter table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 23).
 */
int aq_mss_set_egress_ctlf_record(struct aq_hw_s *hw,
				  const struct aq_mss_egress_ctlf_record *rec,
				  u16 table_index);

/*!  Read the raw table data from the specified row of the Egress
 *   Packet Classifier table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 47).
 */
int aq_mss_get_egress_class_record(struct aq_hw_s *hw,
				   struct aq_mss_egress_class_record *rec,
				   u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Egress Packet Classifier table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write (max 47).
 */
int aq_mss_set_egress_class_record(struct aq_hw_s *hw,
				   const struct aq_mss_egress_class_record *rec,
				   u16 table_index);

/*!  Read the raw table data from the specified row of the Egress SC
 *   Lookup table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 31).
 */
int aq_mss_get_egress_sc_record(struct aq_hw_s *hw,
				struct aq_mss_egress_sc_record *rec,
				u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Egress SC Lookup table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write (max 31).
 */
int aq_mss_set_egress_sc_record(struct aq_hw_s *hw,
				const struct aq_mss_egress_sc_record *rec,
				u16 table_index);

/*!  Read the raw table data from the specified row of the Egress SA
 *   Lookup table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 31).
 */
int aq_mss_get_egress_sa_record(struct aq_hw_s *hw,
				struct aq_mss_egress_sa_record *rec,
				u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Egress SA Lookup table.
 *  rec  - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write (max 31).
 */
int aq_mss_set_egress_sa_record(struct aq_hw_s *hw,
				const struct aq_mss_egress_sa_record *rec,
				u16 table_index);

/*!  Read the raw table data from the specified row of the Egress SA
 *   Key Lookup table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 31).
 */
int aq_mss_get_egress_sakey_record(struct aq_hw_s *hw,
				   struct aq_mss_egress_sakey_record *rec,
				   u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Egress SA Key Lookup table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write (max 31).
 */
int aq_mss_set_egress_sakey_record(struct aq_hw_s *hw,
				   const struct aq_mss_egress_sakey_record *rec,
				   u16 table_index);

/*!  Read the raw table data from the specified row of the Ingress
 *   Pre-MACSec CTL Filter table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 23).
 */
int aq_mss_get_ingress_prectlf_record(struct aq_hw_s *hw,
				      struct aq_mss_ingress_prectlf_record *rec,
				      u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Ingress Pre-MACSec CTL Filter table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 23).
 */
int aq_mss_set_ingress_prectlf_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_prectlf_record *rec,
	u16 table_index);

/*!  Read the raw table data from the specified row of the Ingress
 *   Pre-MACSec Packet Classifier table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 47).
 */
int aq_mss_get_ingress_preclass_record(struct aq_hw_s *hw,
	struct aq_mss_ingress_preclass_record *rec,
	u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Ingress Pre-MACSec Packet Classifier table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 47).
 */
int aq_mss_set_ingress_preclass_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_preclass_record *rec,
	u16 table_index);

/*!  Read the raw table data from the specified row of the Ingress SC
 *   Lookup table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 31).
 */
int aq_mss_get_ingress_sc_record(struct aq_hw_s *hw,
				 struct aq_mss_ingress_sc_record *rec,
				 u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Ingress SC Lookup table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 31).
 */
int aq_mss_set_ingress_sc_record(struct aq_hw_s *hw,
				 const struct aq_mss_ingress_sc_record *rec,
				 u16 table_index);

/*!  Read the raw table data from the specified row of the Ingress SA
 *   Lookup table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 31).
 */
int aq_mss_get_ingress_sa_record(struct aq_hw_s *hw,
				 struct aq_mss_ingress_sa_record *rec,
				 u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Ingress SA Lookup table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 31).
 */
int aq_mss_set_ingress_sa_record(struct aq_hw_s *hw,
				 const struct aq_mss_ingress_sa_record *rec,
				 u16 table_index);

/*!  Read the raw table data from the specified row of the Ingress SA
 *   Key Lookup table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 31).
 */
int aq_mss_get_ingress_sakey_record(struct aq_hw_s *hw,
				    struct aq_mss_ingress_sakey_record *rec,
				    u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Ingress SA Key Lookup table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 31).
 */
int aq_mss_set_ingress_sakey_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_sakey_record *rec,
	u16 table_index);

/*!  Read the raw table data from the specified row of the Ingress
 *   Post-MACSec Packet Classifier table, and unpack it into the
 *   fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 48).
 */
int aq_mss_get_ingress_postclass_record(struct aq_hw_s *hw,
	struct aq_mss_ingress_postclass_record *rec,
	u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Ingress Post-MACSec Packet Classifier table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 48).
 */
int aq_mss_set_ingress_postclass_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_postclass_record *rec,
	u16 table_index);

/*!  Read the raw table data from the specified row of the Ingress
 *   Post-MACSec CTL Filter table, and unpack it into the fields of rec.
 *  rec - [OUT] The raw table row data will be unpacked into the fields of rec.
 *  table_index - The table row to read (max 23).
 */
int aq_mss_get_ingress_postctlf_record(struct aq_hw_s *hw,
	struct aq_mss_ingress_postctlf_record *rec,
	u16 table_index);

/*!  Pack the fields of rec, and write the packed data into the
 *   specified row of the Ingress Post-MACSec CTL Filter table.
 *  rec - [IN] The bitfield values to write to the table row.
 *  table_index - The table row to write(max 23).
 */
int aq_mss_set_ingress_postctlf_record(struct aq_hw_s *hw,
	const struct aq_mss_ingress_postctlf_record *rec,
	u16 table_index);

/*!  Read the counters for the specified SC, and unpack them into the
 *   fields of counters.
 *  counters - [OUT] The raw table row data will be unpacked here.
 *  sc_index - The table row to read (max 31).
 */
int aq_mss_get_egress_sc_counters(struct aq_hw_s *hw,
				  struct aq_mss_egress_sc_counters *counters,
				  u16 sc_index);

/*!  Read the counters for the specified SA, and unpack them into the
 *   fields of counters.
 *  counters - [OUT] The raw table row data will be unpacked here.
 *  sa_index - The table row to read (max 31).
 */
int aq_mss_get_egress_sa_counters(struct aq_hw_s *hw,
				  struct aq_mss_egress_sa_counters *counters,
				  u16 sa_index);

/*!  Read the counters for the common egress counters, and unpack them
 *   into the fields of counters.
 *  counters - [OUT] The raw table row data will be unpacked here.
 */
int aq_mss_get_egress_common_counters(struct aq_hw_s *hw,
	struct aq_mss_egress_common_counters *counters);

/*!  Clear all Egress counters to 0.*/
int aq_mss_clear_egress_counters(struct aq_hw_s *hw);

/*!  Read the counters for the specified SA, and unpack them into the
 *   fields of counters.
 *  counters - [OUT] The raw table row data will be unpacked here.
 *  sa_index - The table row to read (max 31).
 */
int aq_mss_get_ingress_sa_counters(struct aq_hw_s *hw,
				   struct aq_mss_ingress_sa_counters *counters,
				   u16 sa_index);

/*!  Read the counters for the common ingress counters, and unpack them
 *   into the fields of counters.
 *  counters - [OUT] The raw table row data will be unpacked here.
 */
int aq_mss_get_ingress_common_counters(struct aq_hw_s *hw,
	struct aq_mss_ingress_common_counters *counters);

/*!  Clear all Ingress counters to 0. */
int aq_mss_clear_ingress_counters(struct aq_hw_s *hw);

/*!  Get Egress SA expired. */
int aq_mss_get_egress_sa_expired(struct aq_hw_s *hw, u32 *expired);
/*!  Get Egress SA threshold expired. */
int aq_mss_get_egress_sa_threshold_expired(struct aq_hw_s *hw,
					   u32 *expired);
/*!  Set Egress SA expired. */
int aq_mss_set_egress_sa_expired(struct aq_hw_s *hw, u32 expired);
/*!  Set Egress SA threshold expired. */
int aq_mss_set_egress_sa_threshold_expired(struct aq_hw_s *hw,
					   u32 expired);

#endif /* __MACSEC_API_H__ */
