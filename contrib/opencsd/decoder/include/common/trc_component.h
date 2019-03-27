/*!
 * \file       trc_component.h
 * \brief      OpenCSD : Base trace decode component.
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

#ifndef ARM_TRC_COMPONENT_H_INCLUDED
#define ARM_TRC_COMPONENT_H_INCLUDED

#include <string>
#include "comp_attach_pt_t.h"
#include "interfaces/trc_error_log_i.h"
#include "ocsd_error.h"

class errLogAttachMonitor;

/** @addtogroup ocsd_infrastructure
@{*/

/*!
 * @class TraceComponent   
 * @brief Base class for all decode components in the library.
 * 
 * Provides error logging attachment point and component type and instance naming
 * Interface for handling of component operational mode. 
 */
class TraceComponent 
{
public:
    TraceComponent(const std::string &name);
    TraceComponent(const std::string &name, int instIDNum);
    virtual ~TraceComponent(); /**< Default Destructor */

    const std::string &getComponentName() const { return m_name; };
    void setComponentName(const std::string &name)  { m_name = name; };

    /** Error logger attachment point.*/
    componentAttachPt<ITraceErrorLog> *getErrorLogAttachPt() { return &m_error_logger; };

    /*!
     * Set the operational mode for the component.
     * This controls the way the component behaves under error conditions etc.
     * These flags may also control output formats or data.
     * Operation mode flags used are component specific and defined by derived classes.
     *
     * @param op_flags : Set of operation mode flags.
     *
     * @return ocsd_err_t  : OCSD_OK if flags supported by this component, error if unsuppored 
     */
    ocsd_err_t setComponentOpMode(uint32_t op_flags);

    /*!
     * Return the current operational mode flags values
     * 
     * @return const uint32_t  : Op Mode flags.
     */
    const uint32_t getComponentOpMode() const { return m_op_flags; };

    /*!
     * Get the supported operational mode flags for this component.
     * Base class will return nothing supported. 
     * Derived class must set the value correctly for the component.
     *
     * @return const uint32_t  : Supported flags values.
     */
    const uint32_t getSupportedOpModes() const { return m_supported_op_flags; };  

    /*!
     * Set associated trace component - used by generic code to track 
     * packet processor / packet decoder pairs.
     *
     * @param *assocComp : pointer to the associated component
     */
    void setAssocComponent(TraceComponent *assocComp) {  m_assocComp = assocComp; };


    /*!
     * get associated trace component pointer
     *
     * @return TraceComponent  *: associated component.
     */
    TraceComponent *getAssocComponent() { return m_assocComp; };

    /*!
     * Log a message at the default severity on this component.
     */
    void LogDefMessage(const std::string &msg)
    {
        LogMessage(m_errVerbosity, msg);
    }

protected:
    friend class errLogAttachMonitor;

    void LogError(const ocsdError &Error);
    void LogMessage(const ocsd_err_severity_t filter_level, const std::string &msg);
    const ocsd_err_severity_t getErrorLogLevel() const { return m_errVerbosity; };
    const bool isLoggingErrorLevel(const ocsd_err_severity_t level) const { return level <= m_errVerbosity; };
    void updateErrorLogLevel(); 

    void do_attach_notify(const int num_attached);
    void Init(const std::string &name);

    uint32_t m_op_flags;                //!< current component operational mode flags.
    uint32_t m_supported_op_flags;      //!< supported component operational mode flags - derived class to intialise.

private:
    componentAttachPt<ITraceErrorLog> m_error_logger;
    ocsd_hndl_err_log_t m_errLogHandle;
    ocsd_err_severity_t m_errVerbosity;
    errLogAttachMonitor *m_pErrAttachMon;

    std::string m_name; 

    TraceComponent *m_assocComp;    //!< associated component -> if this is a pkt decoder, associated pkt processor.
};
/** @}*/
#endif // ARM_TRC_COMPONENT_H_INCLUDED

/* End of File trc_component.h */
