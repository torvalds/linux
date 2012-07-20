#ifndef CSR_SDIO_H__
#define CSR_SDIO_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"
#include "csr_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Result Codes */
#define CSR_SDIO_RESULT_INVALID_VALUE   ((CsrResult) 1) /* Invalid argument value */
#define CSR_SDIO_RESULT_NO_DEVICE       ((CsrResult) 2) /* The specified device is no longer present */
#define CSR_SDIO_RESULT_CRC_ERROR       ((CsrResult) 3) /* The transmitted/received data or command response contained a CRC error */
#define CSR_SDIO_RESULT_TIMEOUT         ((CsrResult) 4) /* No command response or data received from device, or function enable/disable did not succeed within timeout period */
#define CSR_SDIO_RESULT_NOT_RESET       ((CsrResult) 5) /* The device was not reset */

/* Features (for use in features member of CsrSdioFunction) */
#define CSR_SDIO_FEATURE_BYTE_MODE                   0x00000001 /* Transfer sizes do not have to be a multiple of block size */
#define CSR_SDIO_FEATURE_DMA_CAPABLE_MEM_REQUIRED    0x00000002 /* Bulk operations require DMA friendly memory */

/* CsrSdioFunctionId wildcards (for use in CsrSdioFunctionId members) */
#define CSR_SDIO_ANY_MANF_ID        0xFFFF
#define CSR_SDIO_ANY_CARD_ID        0xFFFF
#define CSR_SDIO_ANY_SDIO_FUNCTION  0xFF
#define CSR_SDIO_ANY_SDIO_INTERFACE 0xFF

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioFunctionId
 *
 *  DESCRIPTION
 *      This structure describes one or more functions of a device, based on
 *      four qualitative measures. The CsrSdioFunctionId wildcard defines can be
 *      used for making the CsrSdioFunctionId match more than one function.
 *
 *  MEMBERS
 *      manfId - Vendor ID (or CSR_SDIO_ANY_MANF_ID).
 *      cardId - Device ID (or CSR_SDIO_ANY_CARD_ID).
 *      sdioFunction - SDIO Function number (or CSR_SDIO_ANY_SDIO_FUNCTION).
 *      sdioInterface - SDIO Standard Interface Code (or CSR_SDIO_ANY_SDIO_INTERFACE)
 *
 *----------------------------------------------------------------------------*/
typedef struct
{
    CsrUint16 manfId;       /* Vendor ID to match or CSR_SDIO_ANY_MANF_ID */
    CsrUint16 cardId;       /* Device ID to match or CSR_SDIO_ANY_CARD_ID */
    u8  sdioFunction; /* SDIO Function number to match or CSR_SDIO_ANY_SDIO_FUNCTION */
    u8  sdioInterface; /* SDIO Standard Interface Code to match or CSR_SDIO_ANY_SDIO_INTERFACE */
} CsrSdioFunctionId;

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioFunction
 *
 *  DESCRIPTION
 *      This structure represents a single function on a device.
 *
 *  MEMBERS
 *      sdioId - A CsrSdioFunctionId describing this particular function. The
 *               subfield shall not contain any CsrSdioFunctionId wildcards. The
 *               subfields shall describe the specific single function
 *               represented by this structure.
 *      blockSize - Actual configured block size, or 0 if unconfigured.
 *      features - Bit mask with any of CSR_SDIO_FEATURE_* set.
 *      device - Handle of device containing the function. If two functions have
 *               the same device handle, they reside on the same device.
 *      driverData - For use by the Function Driver. The SDIO Driver shall not
 *                   attempt to dereference the pointer.
 *      priv - For use by the SDIO Driver. The Function Driver shall not attempt
 *             to dereference the pointer.
 *
 *
 *----------------------------------------------------------------------------*/
typedef struct
{
    CsrSdioFunctionId sdioId;
    CsrUint16         blockSize; /* Actual configured block size, or 0 if unconfigured */
    CsrUint32         features; /* Bit mask with any of CSR_SDIO_FEATURE_* set */
    void             *device; /* Handle of device containing the function */
    void             *driverData; /* For use by the Function Driver */
    void             *priv; /* For use by the SDIO Driver */
} CsrSdioFunction;

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioInsertedCallback, CsrSdioRemovedCallback
 *
 *  DESCRIPTION
 *      CsrSdioInsertedCallback is called when a function becomes available to
 *      a registered Function Driver that supports the function.
 *      CsrSdioRemovedCallback is called when a function is no longer available
 *      to a Function Driver, either because the device has been removed, or the
 *      Function Driver has been unregistered.
 *
 *      NOTE: These functions are implemented by the Function Driver, and are
 *            passed as function pointers in the CsrSdioFunctionDriver struct.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *
 *----------------------------------------------------------------------------*/
typedef void (*CsrSdioInsertedCallback)(CsrSdioFunction *function);
typedef void (*CsrSdioRemovedCallback)(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioInterruptDsrCallback, CsrSdioInterruptCallback
 *
 *  DESCRIPTION
 *      CsrSdioInterruptCallback is called when an interrupt occurs on the
 *      the device associated with the specified function.
 *
 *      NOTE: These functions are implemented by the Function Driver, and are
 *            passed as function pointers in the CsrSdioFunctionDriver struct.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *
 *  RETURNS (only CsrSdioInterruptCallback)
 *      A pointer to a CsrSdioInterruptDsrCallback function.
 *
 *----------------------------------------------------------------------------*/
typedef void (*CsrSdioInterruptDsrCallback)(CsrSdioFunction *function);
typedef CsrSdioInterruptDsrCallback (*CsrSdioInterruptCallback)(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioSuspendCallback, CsrSdioResumeCallback
 *
 *  DESCRIPTION
 *      CsrSdioSuspendCallback is called when the system is preparing to go
 *      into a suspended state. CsrSdioResumeCallback is called when the system
 *      has entered an active state again.
 *
 *      NOTE: These functions are implemented by the Function Driver, and are
 *            passed as function pointers in the CsrSdioFunctionDriver struct.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *
 *----------------------------------------------------------------------------*/
typedef void (*CsrSdioSuspendCallback)(CsrSdioFunction *function);
typedef void (*CsrSdioResumeCallback)(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioAsyncCallback, CsrSdioAsyncDsrCallback
 *
 *  DESCRIPTION
 *      CsrSdioAsyncCallback is called when an asynchronous operation completes.
 *
 *      NOTE: These functions are implemented by the Function Driver, and are
 *            passed as function pointers in the function calls that initiate
 *            the operation.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *      result - The result of the operation that completed. See the description
 *               of the initiating function for possible result values.
 *
 *  RETURNS (only CsrSdioAsyncCallback)
 *      A pointer to a CsrSdioAsyncDsrCallback function.
 *
 *----------------------------------------------------------------------------*/
typedef void (*CsrSdioAsyncDsrCallback)(CsrSdioFunction *function, CsrResult result);
typedef CsrSdioAsyncDsrCallback (*CsrSdioAsyncCallback)(CsrSdioFunction *function, CsrResult result);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioFunctionDriver
 *
 *  DESCRIPTION
 *      Structure representing a Function Driver.
 *
 *  MEMBERS
 *      inserted - Callback, see description of CsrSdioInsertedCallback.
 *      removed - Callback, see description of CsrSdioRemovedCallback.
 *      intr - Callback, see description of CsrSdioInterruptCallback.
 *      suspend - Callback, see description of CsrSdioSuspendCallback.
 *      resume - Callback, see description of CsrSdioResumeCallback.
 *      ids - Array of CsrSdioFunctionId describing one or more functions that
 *            are supported by the Function Driver.
 *      idsCount - Length of the ids array.
 *      priv - For use by the SDIO Driver. The Function Driver may initialise
 *             it to NULL, but shall otherwise not access the pointer or attempt
 *             to dereference it.
 *
 *----------------------------------------------------------------------------*/
typedef struct
{
    CsrSdioInsertedCallback  inserted;
    CsrSdioRemovedCallback   removed;
    CsrSdioInterruptCallback intr;
    CsrSdioSuspendCallback   suspend;
    CsrSdioResumeCallback    resume;
    CsrSdioFunctionId       *ids;
    u8                 idsCount;
    void                    *priv;          /* For use by the SDIO Driver */
} CsrSdioFunctionDriver;

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioFunctionDriverRegister
 *
 *  DESCRIPTION
 *      Register a Function Driver.
 *
 *  PARAMETERS
 *      functionDriver - Pointer to struct describing the Function Driver.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The Function Driver was successfully
 *                           registered.
 *      CSR_RESULT_FAILURE - Unable to register the function driver,
 *                                because of an unspecified/unknown error. The
 *                                Function Driver has not been registered.
 *      CSR_SDIO_RESULT_INVALID_VALUE - The specified Function Driver pointer
 *                                      does not point at a valid Function
 *                                      Driver structure, or some of the members
 *                                      contain invalid entries.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioFunctionDriverRegister(CsrSdioFunctionDriver *functionDriver);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioFunctionDriverUnregister
 *
 *  DESCRIPTION
 *      Unregister a previously registered Function Driver.
 *
 *  PARAMETERS
 *      functionDriver - pointer to struct describing the Function Driver.
 *
 *----------------------------------------------------------------------------*/
void CsrSdioFunctionDriverUnregister(CsrSdioFunctionDriver *functionDriver);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioFunctionEnable, CsrSdioFunctionDisable
 *
 *  DESCRIPTION
 *      Enable/disable the specified function by setting/clearing the
 *      corresponding bit in the I/O Enable register in function 0, and then
 *      periodically reading the related bit in the I/O Ready register until it
 *      is set/clear, limited by an implementation defined timeout.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The specified function was enabled/disabled.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured. The state of the
 *                                  related bit in the I/O Enable register is
 *                                  undefined.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device, or the related
 *                                bit in the I/O ready register was not
 *                                set/cleared within the timeout period.
 *
 *      NOTE: If the SDIO R5 response is available, and either of the
 *            FUNCTION_NUMBER or OUT_OF_RANGE bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE shall be returned. If the ERROR bit
 *            is set (but none of FUNCTION_NUMBER or OUT_OF_RANGE),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioFunctionEnable(CsrSdioFunction *function);
CsrResult CsrSdioFunctionDisable(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioInterruptEnable, CsrSdioInterruptDisable
 *
 *  DESCRIPTION
 *      Enable/disable the interrupt for the specified function by
 *      setting/clearing the corresponding bit in the INT Enable register in
 *      function 0.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The specified function was enabled/disabled.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured. The state of the
 *                                  related bit in the INT Enable register is
 *                                  unchanged.
 *      CSR_SDIO_RESULT_INVALID_VALUE - The specified function cannot be
 *                                      enabled/disabled, because it either
 *                                      does not exist or it is not possible to
 *                                      individually enable/disable functions.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device.
 *
 *      NOTE: If the SDIO R5 response is available, and either of the
 *            FUNCTION_NUMBER or OUT_OF_RANGE bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE shall be returned. If the ERROR bit
 *            is set (but none of FUNCTION_NUMBER or OUT_OF_RANGE),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioInterruptEnable(CsrSdioFunction *function);
CsrResult CsrSdioInterruptDisable(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioInterruptAcknowledge
 *
 *  DESCRIPTION
 *      Acknowledge that a signalled interrupt has been handled. Shall only
 *      be called once, and exactly once for each signalled interrupt to the
 *      corresponding function.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function to which the
 *                 event was signalled.
 *
 *----------------------------------------------------------------------------*/
void CsrSdioInterruptAcknowledge(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioInsertedAcknowledge, CsrSdioRemovedAcknowledge
 *
 *  DESCRIPTION
 *      Acknowledge that a signalled inserted/removed event has been handled.
 *      Shall only be called once, and exactly once for each signalled event to
 *      the corresponding function.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function to which the
 *                 inserted was signalled.
 *      result (CsrSdioInsertedAcknowledge only)
 *          CSR_RESULT_SUCCESS - The Function Driver has accepted the
 *                                    function, and the function is attached to
 *                                    the Function Driver until the
 *                                    CsrSdioRemovedCallback is called and
 *                                    acknowledged.
 *          CSR_RESULT_FAILURE - Unable to accept the function. The
 *                                    function is not attached to the Function
 *                                    Driver, and it may be passed to another
 *                                    Function Driver which supports the
 *                                    function.
 *
 *----------------------------------------------------------------------------*/
void CsrSdioInsertedAcknowledge(CsrSdioFunction *function, CsrResult result);
void CsrSdioRemovedAcknowledge(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioSuspendAcknowledge, CsrSdioResumeAcknowledge
 *
 *  DESCRIPTION
 *      Acknowledge that a signalled suspend event has been handled. Shall only
 *      be called once, and exactly once for each signalled event to the
 *      corresponding function.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function to which the
 *                 event was signalled.
 *      result
 *          CSR_RESULT_SUCCESS - Successfully suspended/resumed.
 *          CSR_RESULT_FAILURE - Unspecified/unknown error.
 *
 *----------------------------------------------------------------------------*/
void CsrSdioSuspendAcknowledge(CsrSdioFunction *function, CsrResult result);
void CsrSdioResumeAcknowledge(CsrSdioFunction *function, CsrResult result);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioBlockSizeSet
 *
 *  DESCRIPTION
 *      Set the block size to use for the function. The actual configured block
 *      size shall be the minimum of:
 *          1) Maximum block size supported by the function.
 *          2) Maximum block size supported by the host controller.
 *          3) The block size specified by the blockSize argument.
 *
 *      When this function returns, the actual configured block size is
 *      available in the blockSize member of the function struct.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *      blockSize - Block size to use for the function. Valid range is 1 to
 *                  2048.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The block size register on the chip
 *                                was updated.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_INVALID_VALUE - One or more arguments were invalid.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured. The configured block
 *                                  size is undefined.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device.
 *
 *      NOTE: If the SDIO R5 response is available, and the FUNCTION_NUMBER
 *            bits is set, CSR_SDIO_RESULT_INVALID_VALUE shall be returned.
 *            If the ERROR bit is set (but not FUNCTION_NUMBER),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *      NOTE: Setting the block size requires two individual operations. The
 *            implementation shall ignore the OUT_OF_RANGE bit of the SDIO R5
 *            response for the first operation, as the partially configured
 *            block size may be out of range, even if the final block size
 *            (after the second operation) is in the valid range.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioBlockSizeSet(CsrSdioFunction *function, CsrUint16 blockSize);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioMaxBusClockFrequencySet
 *
 *  DESCRIPTION
 *      Set the maximum clock frequency to use for the device associated with
 *      the specified function. The actual configured clock frequency for the
 *      device shall be the minimum of:
 *          1) Maximum clock frequency supported by the device.
 *          2) Maximum clock frequency supported by the host controller.
 *          3) Maximum clock frequency specified for any function on the same
 *             device.
 *
 *      If the clock frequency exceeds 25MHz, it is the responsibility of the
 *      SDIO driver to enable high speed mode on the device, using the standard
 *      defined procedure, before increasing the frequency beyond the limit.
 *
 *      Note that the clock frequency configured affects all functions on the
 *      same device.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *      maxFrequency - The maximum clock frequency for the function in Hertz.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The maximum clock frequency was succesfully
 *                                set for the function.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_INVALID_VALUE - One or more arguments were invalid.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *
 *      NOTE: If the SDIO R5 response is available, and the FUNCTION_NUMBER
 *            bits is set, CSR_SDIO_RESULT_INVALID_VALUE shall be returned.
 *            If the ERROR bit is set (but not FUNCTION_NUMBER),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioMaxBusClockFrequencySet(CsrSdioFunction *function, CsrUint32 maxFrequency);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioRead8, CsrSdioWrite8, CsrSdioRead8Async, CsrSdioWrite8Async
 *
 *  DESCRIPTION
 *      Read/write an 8bit value from/to the specified register address.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *      address - Register address within the function.
 *      data - The data to read/write.
 *      callback - The function to call on operation completion.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The data was successfully read/written.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_INVALID_VALUE - One or more arguments were invalid.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured. No data read/written.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device.
 *
 *      NOTE: If the SDIO R5 response is available, and either of the
 *            FUNCTION_NUMBER or OUT_OF_RANGE bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE shall be returned. If the ERROR bit
 *            is set (but none of FUNCTION_NUMBER or OUT_OF_RANGE),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *      NOTE: The CsrSdioRead8Async and CsrSdioWrite8Async functions return
 *            immediately, and the supplied callback function is called when the
 *            operation is complete. The result value is given as an argument to
 *            the callback function.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioRead8(CsrSdioFunction *function, CsrUint32 address, u8 *data);
CsrResult CsrSdioWrite8(CsrSdioFunction *function, CsrUint32 address, u8 data);
void CsrSdioRead8Async(CsrSdioFunction *function, CsrUint32 address, u8 *data, CsrSdioAsyncCallback callback);
void CsrSdioWrite8Async(CsrSdioFunction *function, CsrUint32 address, u8 data, CsrSdioAsyncCallback callback);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioRead16, CsrSdioWrite16, CsrSdioRead16Async, CsrSdioWrite16Async
 *
 *  DESCRIPTION
 *      Read/write a 16bit value from/to the specified register address.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *      address - Register address within the function.
 *      data - The data to read/write.
 *      callback - The function to call on operation completion.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The data was successfully read/written.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_INVALID_VALUE - One or more arguments were invalid.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured. Data may have been
 *                                  partially read/written.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device.
 *
 *      NOTE: If the SDIO R5 response is available, and either of the
 *            FUNCTION_NUMBER or OUT_OF_RANGE bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE shall be returned. If the ERROR bit
 *            is set (but none of FUNCTION_NUMBER or OUT_OF_RANGE),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *      NOTE: The CsrSdioRead16Async and CsrSdioWrite16Async functions return
 *            immediately, and the supplied callback function is called when the
 *            operation is complete. The result value is given as an argument to
 *            the callback function.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioRead16(CsrSdioFunction *function, CsrUint32 address, CsrUint16 *data);
CsrResult CsrSdioWrite16(CsrSdioFunction *function, CsrUint32 address, CsrUint16 data);
void CsrSdioRead16Async(CsrSdioFunction *function, CsrUint32 address, CsrUint16 *data, CsrSdioAsyncCallback callback);
void CsrSdioWrite16Async(CsrSdioFunction *function, CsrUint32 address, CsrUint16 data, CsrSdioAsyncCallback callback);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioF0Read8, CsrSdioF0Write8, CsrSdioF0Read8Async,
 *      CsrSdioF0Write8Async
 *
 *  DESCRIPTION
 *      Read/write an 8bit value from/to the specified register address in
 *      function 0.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *      address - Register address within the function.
 *      data - The data to read/write.
 *      callback - The function to call on operation completion.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The data was successfully read/written.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_INVALID_VALUE - One or more arguments were invalid.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured. No data read/written.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device.
 *
 *      NOTE: If the SDIO R5 response is available, and either of the
 *            FUNCTION_NUMBER or OUT_OF_RANGE bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE shall be returned. If the ERROR bit
 *            is set (but none of FUNCTION_NUMBER or OUT_OF_RANGE),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *      NOTE: The CsrSdioF0Read8Async and CsrSdioF0Write8Async functions return
 *            immediately, and the supplied callback function is called when the
 *            operation is complete. The result value is given as an argument to
 *            the callback function.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioF0Read8(CsrSdioFunction *function, CsrUint32 address, u8 *data);
CsrResult CsrSdioF0Write8(CsrSdioFunction *function, CsrUint32 address, u8 data);
void CsrSdioF0Read8Async(CsrSdioFunction *function, CsrUint32 address, u8 *data, CsrSdioAsyncCallback callback);
void CsrSdioF0Write8Async(CsrSdioFunction *function, CsrUint32 address, u8 data, CsrSdioAsyncCallback callback);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioRead, CsrSdioWrite, CsrSdioReadAsync, CsrSdioWriteAsync
 *
 *  DESCRIPTION
 *      Read/write a specified number of bytes from/to the specified register
 *      address.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *      address - Register address within the function.
 *      data - The data to read/write.
 *      length - Number of byte to read/write.
 *      callback - The function to call on operation completion.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - The data was successfully read/written.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_INVALID_VALUE - One or more arguments were invalid.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured. Data may have been
 *                                  partially read/written.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device.
 *
 *      NOTE: If the SDIO R5 response is available, and either of the
 *            FUNCTION_NUMBER or OUT_OF_RANGE bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE shall be returned. If the ERROR bit
 *            is set (but none of FUNCTION_NUMBER or OUT_OF_RANGE),
 *            CSR_RESULT_FAILURE shall be returned. The ILLEGAL_COMMAND and
 *            COM_CRC_ERROR bits shall be ignored.
 *
 *            If the CSPI response is available, and any of the
 *            FUNCTION_DISABLED or CLOCK_DISABLED bits are set,
 *            CSR_SDIO_RESULT_INVALID_VALUE will be returned.
 *
 *      NOTE: The CsrSdioF0Read8Async and CsrSdioF0Write8Async functions return
 *            immediately, and the supplied callback function is called when the
 *            operation is complete. The result value is given as an argument to
 *            the callback function.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioRead(CsrSdioFunction *function, CsrUint32 address, void *data, CsrUint32 length);
CsrResult CsrSdioWrite(CsrSdioFunction *function, CsrUint32 address, const void *data, CsrUint32 length);
void CsrSdioReadAsync(CsrSdioFunction *function, CsrUint32 address, void *data, CsrUint32 length, CsrSdioAsyncCallback callback);
void CsrSdioWriteAsync(CsrSdioFunction *function, CsrUint32 address, const void *data, CsrUint32 length, CsrSdioAsyncCallback callback);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioPowerOn, CsrSdioPowerOff
 *
 *  DESCRIPTION
 *      Power on/off the device.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function that resides on
 *                 the device to power on/off.
 *
 *  RETURNS (only CsrSdioPowerOn)
 *      CSR_RESULT_SUCCESS - Power was succesfully reapplied and the device
 *                                has been reinitialised.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured during reinitialisation.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device during
 *                                reinitialisation.
 *      CSR_SDIO_RESULT_NOT_RESET - The power was not removed by the
 *                                  CsrSdioPowerOff call. The state of the
 *                                  device is unchanged.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioPowerOn(CsrSdioFunction *function);
void CsrSdioPowerOff(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioHardReset
 *
 *  DESCRIPTION
 *      Perform a hardware reset of the device.
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function that resides on
 *                 the device to hard reset.
 *
 *  RETURNS
 *      CSR_RESULT_SUCCESS - Reset was succesfully performed and the device
 *                                has been reinitialised.
 *      CSR_RESULT_FAILURE - Unspecified/unknown error.
 *      CSR_SDIO_RESULT_NO_DEVICE - The device does not exist anymore.
 *      CSR_SDIO_RESULT_CRC_ERROR - A CRC error occured during reinitialisation.
 *      CSR_SDIO_RESULT_TIMEOUT - No response from the device during
 *                                reinitialisation.
 *      CSR_SDIO_RESULT_NOT_RESET - The reset was not applied because it is not
 *                                  supported. The state of the device is
 *                                  unchanged.
 *
 *----------------------------------------------------------------------------*/
CsrResult CsrSdioHardReset(CsrSdioFunction *function);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSdioFunctionActive, CsrSdioFunctionIdle
 *
 *  DESCRIPTION
 *
 *  PARAMETERS
 *      function - Pointer to struct representing the function.
 *
 *----------------------------------------------------------------------------*/
void CsrSdioFunctionActive(CsrSdioFunction *function);
void CsrSdioFunctionIdle(CsrSdioFunction *function);

#ifdef __cplusplus
}
#endif

#endif
