/*
 * \file       trc_cmp_cfg_etmv3.h
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

#ifndef ARM_TRC_CMP_CFG_ETMV3_H_INCLUDED
#define ARM_TRC_CMP_CFG_ETMV3_H_INCLUDED

#include "trc_pkt_types_etmv3.h"
#include "common/trc_cs_config.h"


/** @addtogroup ocsd_protocol_cfg
@{*/

/** @name ETMV3 configuration
@{*/


/*!
 * @class EtmV3Config   
 * @brief Interpreter class for etm v3 config structure.
 *
 * Provides quick value interpretation methods for the ETMv3 config register values.
 * Primarily inlined for efficient code.
 * 
 */
class EtmV3Config : public CSConfig
{
public:
    EtmV3Config(); /**< Default constructor */
    EtmV3Config(const ocsd_etmv3_cfg *cfg_regs);
    ~EtmV3Config() {}; /**< Default destructor */

    /* register bit constants. */
    static const uint32_t CTRL_DATAVAL  = 0x4;
    static const uint32_t CTRL_DATAADDR = 0x8;
    static const uint32_t CTRL_CYCLEACC = 0x1000;
    static const uint32_t CTRL_DATAONLY = 0x100000;
    static const uint32_t CTRL_TS_ENA   = (0x1 << 28);
    static const uint32_t CTRL_VMID_ENA = (0x1 << 30);

    static const uint32_t CCER_HAS_TS   = (0x1 << 22);
    static const uint32_t CCER_VIRTEXT  = (0x1 << 26);
    static const uint32_t CCER_TS64BIT  = (0x1 << 29);

    static const uint32_t IDR_ALTBRANCH = 0x100000;

// operations to convert to and from C-API structure

    //! copy assignment operator for C-API base structure into class.
    EtmV3Config & operator=(const ocsd_etmv3_cfg *p_cfg);

    //! cast operator returning struct const reference
    operator const ocsd_etmv3_cfg &() const { return m_cfg; };
    //! cast operator returning struct const pointer
    operator const ocsd_etmv3_cfg *() const { return &m_cfg; };

    //! combination enum to describe trace mode.
    enum EtmTraceMode {
		TM_INSTR_ONLY,  //!< instruction only trace
		TM_I_DATA_VAL,  //!< instruction + data value
		TM_I_DATA_ADDR, //!< instruction + data address 
		TM_I_DATA_VAL_ADDR, //!< instr + data value + data address
		TM_DATAONLY_VAL, //!< data value trace
		TM_DATAONLY_ADDR, //!< data address trace		
		TM_DATAONLY_VAL_ADDR //!< data value + address trace
    };

    EtmTraceMode const GetTraceMode() const; //!< return trace mode

    const bool isInstrTrace() const;    //!< instruction trace present.
    const bool isDataValTrace() const;  //!< data value trace present.
    const bool isDataAddrTrace() const; //!< data address trace present.    
    const bool isDataTrace() const;     //!< either or both data trace types present.

    const bool isCycleAcc() const;  //!< return true if cycle accurate tracing enabled.

    const int  MinorRev() const;    //!< return X revision in 3.X

    const bool isV7MArch() const;   //!< source is V7M architecture
    const bool isAltBranch() const; //!< Alternate branch packet encoding used.

    const int  CtxtIDBytes() const; //!< number of context ID bytes traced 1,2,4;
    const bool hasVirtExt() const;  //!< processor has virtualisation extensions.
    const bool isVMIDTrace() const; //!< VMID tracing enabled.

    const bool hasTS() const;       //!< Timestamps implemented in trace.
    const bool isTSEnabled() const; //!< Timestamp trace is enabled. 
    const bool TSPkt64() const;     //!< timestamp packet is 64 bits in size.

    virtual const uint8_t getTraceID() const; //!< CoreSight Trace ID for this device.

    const ocsd_arch_version_t getArchVersion() const;   //!< architecture version
    const ocsd_core_profile_t getCoreProfile() const;   //!< core profile.

private:
    ocsd_etmv3_cfg m_cfg;

};     


/* inlines for the bit interpretations */

inline EtmV3Config & EtmV3Config::operator=(const ocsd_etmv3_cfg *p_cfg)
{
    m_cfg = *p_cfg;
    return *this; 
}

inline const bool  EtmV3Config::isCycleAcc() const
{
    return (bool)((m_cfg.reg_ctrl & CTRL_CYCLEACC) != 0);
}

//! return X revision in 3.X
inline const int EtmV3Config::MinorRev() const
{
    return ((int)m_cfg.reg_idr & 0xF0) >> 4;
}
 
inline const bool EtmV3Config::isInstrTrace() const
{    
    return (bool)((m_cfg.reg_ctrl & CTRL_DATAONLY) == 0);
}  

inline const bool EtmV3Config::isDataValTrace() const
{
    return (bool)((m_cfg.reg_ctrl & CTRL_DATAVAL) != 0);
}

inline const bool EtmV3Config::isDataAddrTrace() const
{
    return (bool)((m_cfg.reg_ctrl & CTRL_DATAADDR) != 0);
}

//! either or both data trace present
inline const bool EtmV3Config::isDataTrace() const 
{ 
    return (bool)((m_cfg.reg_ctrl & (CTRL_DATAADDR | CTRL_DATAVAL)) != 0);
}

inline const bool EtmV3Config::isV7MArch() const 
{    
    return (bool)((m_cfg.arch_ver == ARCH_V7) && (m_cfg.core_prof == profile_CortexM));
}

//! has alternate branch encoding
inline const bool EtmV3Config::isAltBranch() const 
{
    return (bool)(((m_cfg.reg_idr & IDR_ALTBRANCH) != 0) && (MinorRev() >= 4));
}

//! processor implements virtualisation extensions.
inline const bool EtmV3Config::hasVirtExt() const 
{    
    return (bool)((m_cfg.reg_ccer & CCER_VIRTEXT) != 0);
}

//! TS packet is 64 bit.
inline const bool EtmV3Config::TSPkt64() const 
{
    return (bool)((m_cfg.reg_ccer & CCER_TS64BIT) != 0);
}

//! TS implemented.
inline const bool EtmV3Config::hasTS() const 
{
    return (bool)((m_cfg.reg_ccer & CCER_HAS_TS) != 0);
}

//! TS is enabled in the trace
inline const bool EtmV3Config::isTSEnabled() const 
{
    return (bool)((m_cfg.reg_ctrl & CTRL_TS_ENA) != 0);
}

//! tracing VMID
inline const bool EtmV3Config::isVMIDTrace() const 
{ 
    return (bool)((m_cfg.reg_ctrl & CTRL_VMID_ENA) != 0);
}

inline const uint8_t EtmV3Config::getTraceID() const
{
    return (uint8_t)(m_cfg.reg_trc_id & 0x7F);
}

inline const ocsd_arch_version_t EtmV3Config::getArchVersion() const
{
    return m_cfg.arch_ver;
}

inline const ocsd_core_profile_t EtmV3Config::getCoreProfile() const
{
    return m_cfg.core_prof;
}

/** @}*/

/** @}*/

#endif // ARM_TRC_CMP_CFG_ETMV3_H_INCLUDED

/* End of File trc_cmp_cfg_etmv3.h */
