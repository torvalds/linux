/*
 * \file       trc_cmp_cfg_ptm.h
 * \brief      OpenCSD : 
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */


/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#ifndef ARM_TRC_CMP_CFG_PTM_H_INCLUDED
#define ARM_TRC_CMP_CFG_PTM_H_INCLUDED

#include "trc_pkt_types_ptm.h"
#include "common/trc_cs_config.h"

/** @defgroup ocsd_protocol_cfg  OpenCSD Library : Trace Source Protocol Configuration.

    @brief Classes describing the trace capture time configuration of the trace source hardware.

    Protocol configuration represents the trace capture time settings for the CoreSight hardware
    component generating the trace. The packet processors and packet decoders require this configuration 
    information to correctly interpret packets and decode trace.

@{*/

/** @name PTM configuration
@{*/

/*!
 * @class PtmConfig   
 * @brief Interpreter class for PTM Hardware configuration.
 * 
 * Provides quick value interpretation methods for the PTM config register values.
 * Primarily inlined for efficient code.
 */
class PtmConfig : public CSConfig // public ocsd_ptm_cfg
{
public:
    PtmConfig();    /**< Default constructor */
    PtmConfig(const ocsd_ptm_cfg *cfg_regs);
    ~PtmConfig() {}; /**< Default destructor */

    /* register bit constants. */
    static const uint32_t CTRL_BRANCH_BCAST = (0x1 << 8);
    static const uint32_t CTRL_CYCLEACC     = (0x1 << 12);
    static const uint32_t CTRL_TS_ENA       = (0x1 << 28);
    static const uint32_t CTRL_RETSTACK_ENA = (0x1 << 29);
    static const uint32_t CTRL_VMID_ENA     = (0x1 << 30);

    static const uint32_t CCER_TS_IMPL      = (0x1 << 22);
    static const uint32_t CCER_RESTACK_IMPL = (0x1 << 23);
    static const uint32_t CCER_DMSB_WPT     = (0x1 << 24);
    static const uint32_t CCER_TS_DMSB      = (0x1 << 25);
    static const uint32_t CCER_VIRTEXT      = (0x1 << 26);
    static const uint32_t CCER_TS_ENC_NAT   = (0x1 << 28);
    static const uint32_t CCER_TS_64BIT     = (0x1 << 29);

// operations to convert to and from C-API structure

    //! copy assignment operator for base structure into class.
    PtmConfig & operator=(const ocsd_ptm_cfg *p_cfg);
 
    //! cast operator returning struct const reference
    operator const ocsd_ptm_cfg &() const { return m_cfg; };
    //! cast operator returning struct const pointer
    operator const ocsd_ptm_cfg *() const { return &m_cfg; };

// access functions

    const bool enaBranchBCast() const; //!< Branch broadcast enabled.
    const bool enaCycleAcc() const;  //!< cycle accurate tracing enabled.

    const bool enaRetStack() const;  //!< return stack enabled. 
    const bool hasRetStack() const;  //!< return stack implemented.

    const int  MinorRev() const;    //!< return X revision in 1.X

    const bool hasTS() const;       //!< Timestamps implemented in trace.
    const bool enaTS() const; //!< Timestamp trace is enabled. 
    const bool TSPkt64() const;     //!< timestamp packet is 64 bits in size.
    const bool TSBinEnc() const;  //!< Timestamp encoded as natural binary number.

    const int  CtxtIDBytes() const; //!< number of context ID bytes traced 1,2,4;
    const bool hasVirtExt() const;  //!< processor has virtualisation extensions.
    const bool enaVMID() const; //!< VMID tracing enabled.

    const bool dmsbGenTS() const;   //!< TS generated for DMB and DSB 
    const bool dmsbWayPt() const;   //!< DMB and DSB are waypoint instructions.

    virtual const uint8_t getTraceID() const; //!< CoreSight Trace ID for this device.

    const ocsd_core_profile_t &coreProfile() const { return m_cfg.core_prof; };
    const ocsd_arch_version_t &archVersion() const { return m_cfg.arch_ver; };

private:
    ocsd_ptm_cfg m_cfg;
};

/* inlines */

inline PtmConfig & PtmConfig::operator=(const ocsd_ptm_cfg *p_cfg)
{
    // object of base class ocsd_ptm_cfg 
    m_cfg = *p_cfg;
    return *this;
}

inline const bool PtmConfig::enaBranchBCast() const
{
    return (bool)((m_cfg.reg_ctrl & CTRL_BRANCH_BCAST) != 0);
}

inline const bool PtmConfig::enaCycleAcc() const
{
    return (bool)((m_cfg.reg_ctrl & CTRL_CYCLEACC) != 0);
}

inline const bool PtmConfig::enaRetStack() const
{
    return (bool)((m_cfg.reg_ctrl & CTRL_RETSTACK_ENA) != 0);
}

inline const bool PtmConfig::hasRetStack() const
{
    return (bool)((m_cfg.reg_ccer & CCER_RESTACK_IMPL) != 0);
}

inline const int PtmConfig::MinorRev() const    
{
    return ((int)m_cfg.reg_idr & 0xF0) >> 4;
}

inline const bool PtmConfig::hasTS() const
{
    return (bool)((m_cfg.reg_ccer & CCER_TS_IMPL) != 0);
}

inline const bool PtmConfig::enaTS() const         
{
    return (bool)((m_cfg.reg_ctrl & CTRL_TS_ENA) != 0);
}

inline const bool PtmConfig::TSPkt64() const       
{
    if(MinorRev() == 0) return false;
    return (bool)((m_cfg.reg_ccer & CCER_TS_64BIT) != 0);
}

inline const bool PtmConfig::TSBinEnc() const      
{
    if(MinorRev() == 0) return false;
    return (bool)((m_cfg.reg_ccer & CCER_TS_ENC_NAT) != 0);
}

inline const bool PtmConfig::hasVirtExt() const    
{
    return (bool)((m_cfg.reg_ccer & CCER_VIRTEXT) != 0);
}

inline const bool PtmConfig::enaVMID() const       
{
    return (bool)((m_cfg.reg_ctrl & CTRL_VMID_ENA) != 0);
}

inline const bool PtmConfig::dmsbGenTS() const     
{
    return (bool)((m_cfg.reg_ccer & CCER_TS_DMSB) != 0);
}

inline const bool PtmConfig::dmsbWayPt() const     
{
    return (bool)((m_cfg.reg_ccer & CCER_DMSB_WPT) != 0);
}

inline const uint8_t PtmConfig::getTraceID() const
{
    return (uint8_t)(m_cfg.reg_trc_id & 0x7F);
}

/** @}*/
/** @}*/
#endif // ARM_TRC_CMP_CFG_PTM_H_INCLUDED

/* End of File trc_cmp_cfg_ptm.h */
