#include "../builtins/assembly.h"

    .text
    /* The variable containing the handler function pointer */
    .global _ZN6__xray19XRayPatchedFunctionE
    /* Word-aligned function entry point */
    .p2align 2
    /* Let C/C++ see the symbol */
    .global __xray_FunctionEntry
    .type __xray_FunctionEntry, %function
    /* In C++ it is void extern "C" __xray_FunctionEntry(uint32_t FuncId) with
         FuncId passed in W0 register. */
__xray_FunctionEntry:
    /* Move the return address beyond the end of sled data. The 12 bytes of
         data are inserted in the code of the runtime patch, between the call
         instruction and the instruction returned into. The data contains 32
         bits of instrumented function ID and 64 bits of the address of
         the current trampoline. */
    ADD X30, X30, #12
    /* Push the registers which may be modified by the handler function */
    STP X1, X2, [SP, #-16]!
    STP X3, X4, [SP, #-16]!
    STP X5, X6, [SP, #-16]!
    STP X7, X30, [SP, #-16]!
    STP Q0, Q1, [SP, #-32]!
    STP Q2, Q3, [SP, #-32]!
    STP Q4, Q5, [SP, #-32]!
    STP Q6, Q7, [SP, #-32]!
    /* Load the address of _ZN6__xray19XRayPatchedFunctionE into X1 */
    LDR X1, =_ZN6__xray19XRayPatchedFunctionE
    /* Load the handler function pointer into X2 */
    LDR X2, [X1]
    /* Handler address is nullptr if handler is not set */
    CMP X2, #0
    BEQ FunctionEntry_restore
    /* Function ID is already in W0 (the first parameter).
         X1=0 means that we are tracing an entry event */
    MOV X1, #0
    /* Call the handler with 2 parameters in W0 and X1 */
    BLR X2
FunctionEntry_restore:
    /* Pop the saved registers */
    LDP Q6, Q7, [SP], #32
    LDP Q4, Q5, [SP], #32
    LDP Q2, Q3, [SP], #32
    LDP Q0, Q1, [SP], #32
    LDP X7, X30, [SP], #16
    LDP X5, X6, [SP], #16
    LDP X3, X4, [SP], #16
    LDP X1, X2, [SP], #16
    RET

    /* Word-aligned function entry point */
    .p2align 2
    /* Let C/C++ see the symbol */
    .global __xray_FunctionExit
    .type __xray_FunctionExit, %function
    /* In C++ it is void extern "C" __xray_FunctionExit(uint32_t FuncId) with
         FuncId passed in W0 register. */
__xray_FunctionExit:
    /* Move the return address beyond the end of sled data. The 12 bytes of
         data are inserted in the code of the runtime patch, between the call
         instruction and the instruction returned into. The data contains 32
         bits of instrumented function ID and 64 bits of the address of
         the current trampoline. */
    ADD X30, X30, #12
    /* Push the registers which may be modified by the handler function */
    STP X1, X2, [SP, #-16]!
    STP X3, X4, [SP, #-16]!
    STP X5, X6, [SP, #-16]!
    STP X7, X30, [SP, #-16]!
    STR Q0, [SP, #-16]!
    /* Load the address of _ZN6__xray19XRayPatchedFunctionE into X1 */
    LDR X1, =_ZN6__xray19XRayPatchedFunctionE
    /* Load the handler function pointer into X2 */
    LDR X2, [X1]
    /* Handler address is nullptr if handler is not set */
    CMP X2, #0
    BEQ FunctionExit_restore
    /* Function ID is already in W0 (the first parameter).
         X1=1 means that we are tracing an exit event */
    MOV X1, #1
    /* Call the handler with 2 parameters in W0 and X1 */
    BLR X2
FunctionExit_restore:
    LDR Q0, [SP], #16
    LDP X7, X30, [SP], #16
    LDP X5, X6, [SP], #16
    LDP X3, X4, [SP], #16
    LDP X1, X2, [SP], #16
    RET

    /* Word-aligned function entry point */
    .p2align 2
    /* Let C/C++ see the symbol */
    .global __xray_FunctionTailExit
    .type __xray_FunctionTailExit, %function
    /* In C++ it is void extern "C" __xray_FunctionTailExit(uint32_t FuncId)
         with FuncId passed in W0 register. */
__xray_FunctionTailExit:
    /* Move the return address beyond the end of sled data. The 12 bytes of
         data are inserted in the code of the runtime patch, between the call
         instruction and the instruction returned into. The data contains 32
         bits of instrumented function ID and 64 bits of the address of
         the current trampoline. */
    ADD X30, X30, #12
    /* Push the registers which may be modified by the handler function */
    STP X1, X2, [SP, #-16]!
    STP X3, X4, [SP, #-16]!
    STP X5, X6, [SP, #-16]!
    STP X7, X30, [SP, #-16]!
    /* Push the parameters of the tail called function */
    STP Q0, Q1, [SP, #-32]!
    STP Q2, Q3, [SP, #-32]!
    STP Q4, Q5, [SP, #-32]!
    STP Q6, Q7, [SP, #-32]!
    /* Load the address of _ZN6__xray19XRayPatchedFunctionE into X1 */
    LDR X1, =_ZN6__xray19XRayPatchedFunctionE
    /* Load the handler function pointer into X2 */
    LDR X2, [X1]
    /* Handler address is nullptr if handler is not set */
    CMP X2, #0
    BEQ FunctionTailExit_restore
    /* Function ID is already in W0 (the first parameter).
         X1=2 means that we are tracing a tail exit event, but before the
         logging part of XRay is ready, we pretend that here a normal function
         exit happens, so we give the handler code 1 */
    MOV X1, #1
    /* Call the handler with 2 parameters in W0 and X1 */
    BLR X2
FunctionTailExit_restore:
    /* Pop the parameters of the tail called function */
    LDP Q6, Q7, [SP], #32
    LDP Q4, Q5, [SP], #32
    LDP Q2, Q3, [SP], #32
    LDP Q0, Q1, [SP], #32
    /* Pop the registers which may be modified by the handler function */
    LDP X7, X30, [SP], #16
    LDP X5, X6, [SP], #16
    LDP X3, X4, [SP], #16
    LDP X1, X2, [SP], #16
    RET

NO_EXEC_STACK_DIRECTIVE
