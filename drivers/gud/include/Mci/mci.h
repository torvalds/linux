/** @mainpage MobiCore Control Interface - MCI
 *
 * <h2>Introduction</h2>
 * The MobiCore Control Interface (MCI) is the interface for integrating G&D MobiCore technology into the
 * rich operating system running in the non-secure part of an ARM TrustZone enabled platform.
 *
 * <h2>Interface overview</h2>
 * The Structure of the MobiCore control interface is depicted in the figure below:
 * @image html DoxyOverviewMci500x.png "MobiCore control interface"
 * @image latex DoxyOverviewMci500x.png "MobiCore control interface" width=12cm
 *
 * The MCI is composed of the following interfaces:
 * <ul>
 *
 * <li><b>MobiCore control protocol (MCP) interface.</b></li><br>
 * The MCP interface is responsible for the main communicating with the MobiCore. This involves sending commands for starting
 * and stopping of Trustlets as well as checking their corresponding answers. MCP information is exchanged in a
 * world shared memory buffer which needs to be initially established between NWd and SWd using the FastCall interface.<br>
 *
 * <li><b>Notification queue interface.</b></li><br>
 * Notifications inform the MobiCore runtime environment that information is pending in a WSM buffer.
 * The Trustlet Connector (TLC) and the corresponding Trustlet also utilize this buffer to
 * notify each other about new data within the Trustlet Connector Interface (TCI). Therefore the TLC writes
 * a notification including the session ID to the buffer. The driver informs the MobiCore
 * about the availability of a notification with the use of a SIQ. On the secure side the Runtime Management
 * notifies the Trustlet, according to the given session ID, about the availability of new data.
 * The same mechanism is used vice versa for writing data back to the None-secure world.
 *
 * <li><b>FastCall interface.</b></li><br>
 * The FastCall interface of the MobiCore system is used to transfer control from the Non-secure World (NWd) to the
 * Secure World (SWd) and back. There are three mechanisms the NWd shall use to interact with the MobiCore Monitor:
 * FastCall, N-SIQ and NQ-IRQ (Notification IRQ). FastCall and N-SIQ operations are used to hand over control
 * to the MobiCore. Both functions make use of the SMC [ARM11] operation.
 *
 * </ul>
 *
 * You can find more information about the interfaces in the respective modules description.
 *
 * <h2>Version history</h2>
 * <table class="customtab">
 * <tr><td width="100px"><b>Date</b></td><td width="80px"><b>Version</b></td><td><b>Changes</b></td></tr>
 * <tr><td>2009-06-25</td><td>0.1</td><td>Initial Release</td></tr>
 * <tr><td>2009-07-01</td><td>0.2</td><td>Major rewrite</td></tr>
 * <tr><td>2009-08-06</td><td>0.3</td><td>Added documentation for FastCall helper functions</td></tr>
 * <tr><td>2009-09-10</td><td>0.4</td><td>Update of constant naming. Modification of doxygen config.</td></tr>
 * <tr><td>2010-03-09</td><td>0.5</td><td>Added fastCallPower() helper function for MC_FC_POWER.</td></tr>
 * <tr><td>2010-05-10</td><td>0.6</td><td>Restructuring of load format header.</td></tr>
 * <tr><td>2011-07-19</td><td>0.7</td><td>update to reflect current code changes.</td></tr>
 * </table>
 *
 *
 * @file
 * @defgroup FCI    FastCall Interface
 *
 * @defgroup NQ     Notification Queue
 *
 * @defgroup MCP    MobiCore Control Protocol
 *
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 */
#ifndef MCI_H_
#define MCI_H_

#include "version.h"
#include "mcifc.h"
#include "mcinq.h"
#include "mcimcp.h"

#endif /** MCI_H_ */

/** @} */
