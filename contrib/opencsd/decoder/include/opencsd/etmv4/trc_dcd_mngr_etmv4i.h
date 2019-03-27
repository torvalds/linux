/*
 * \file       trc_dcd_mngr_etmv4i.h
 * \brief      Reference CoreSight Trace Decoder : 
 * 
 * \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
 */

#ifndef ARM_TRC_DCD_MNGR_ETMV4I_H_INCLUDED
#define ARM_TRC_DCD_MNGR_ETMV4I_H_INCLUDED

#include "common/ocsd_dcd_mngr.h"
#include "trc_pkt_decode_etmv4i.h"
#include "trc_pkt_proc_etmv4.h"
#include "trc_cmp_cfg_etmv4.h"
#include "trc_pkt_types_etmv4.h"

class DecoderMngrEtmV4I : public DecodeMngrFullDcd< EtmV4ITrcPacket, 
                                                    ocsd_etmv4_i_pkt_type,
                                                    EtmV4Config,
                                                    ocsd_etmv4_cfg,
                                                    TrcPktProcEtmV4I,
                                                    TrcPktDecodeEtmV4I>
{
public:
    DecoderMngrEtmV4I(const std::string &name) : DecodeMngrFullDcd(name,OCSD_PROTOCOL_ETMV4I) {};
    virtual ~DecoderMngrEtmV4I() {};
};

#endif // ARM_TRC_DCD_MNGR_ETMV4I_H_INCLUDED

/* End of File trc_dcd_mngr_etmv4i.h */
