//===--- Checkers.td - Static Analyzer Checkers -===-----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

include "CheckerBase.td"

//===----------------------------------------------------------------------===//
// Packages.
//===----------------------------------------------------------------------===//

// The Alpha package is for checkers that have too many false positives to be
// turned on by default. The hierarchy under Alpha should be organized in the
// hierarchy checkers would have had if they were truly at the top level.
// (For example, a Cocoa-specific checker that is alpha should be in
// alpha.osx.cocoa).
def Alpha : Package<"alpha">;

def Core : Package<"core">;
def CoreBuiltin : Package<"builtin">, ParentPackage<Core>;
def CoreUninitialized  : Package<"uninitialized">, ParentPackage<Core>;
def CoreAlpha : Package<"core">, ParentPackage<Alpha>;

// The OptIn package is for checkers that are not alpha and that would normally
// be on by default but where the driver does not have enough information to
// determine when they are applicable. For example, localizability checkers fit
// this criterion because the driver cannot determine whether a project is
// localized or not -- this is best determined at the IDE or build-system level.
//
// The checker hierarchy under OptIn should mirror that in Alpha: checkers
// should be organized as if they were at the top level.
//
// Note: OptIn is *not* intended for checkers that are too noisy to be on by
// default. Such checkers belong in the alpha package.
def OptIn : Package<"optin">;

// In the Portability package reside checkers for finding code that relies on
// implementation-defined behavior. Such checks are wanted for cross-platform
// development, but unwanted for developers who target only a single platform.
def PortabilityOptIn : Package<"portability">, ParentPackage<OptIn>;

def Nullability : Package<"nullability">;

def Cplusplus : Package<"cplusplus">;
def CplusplusAlpha : Package<"cplusplus">, ParentPackage<Alpha>;
def CplusplusOptIn : Package<"cplusplus">, ParentPackage<OptIn>;

def Valist : Package<"valist">;

def DeadCode : Package<"deadcode">;
def DeadCodeAlpha : Package<"deadcode">, ParentPackage<Alpha>;

def Performance : Package<"performance">, ParentPackage<OptIn>;

def Security : Package <"security">;
def InsecureAPI : Package<"insecureAPI">, ParentPackage<Security>;
def SecurityAlpha : Package<"security">, ParentPackage<Alpha>;
def Taint : Package<"taint">, ParentPackage<SecurityAlpha>;

def Unix : Package<"unix">;
def UnixAlpha : Package<"unix">, ParentPackage<Alpha>;
def CString : Package<"cstring">, ParentPackage<Unix>;
def CStringAlpha : Package<"cstring">, ParentPackage<UnixAlpha>;

def OSX : Package<"osx">;
def OSXAlpha : Package<"osx">, ParentPackage<Alpha>;
def OSXOptIn : Package<"osx">, ParentPackage<OptIn>;

def Cocoa : Package<"cocoa">, ParentPackage<OSX>;
def CocoaAlpha : Package<"cocoa">, ParentPackage<OSXAlpha>;
def CocoaOptIn : Package<"cocoa">, ParentPackage<OSXOptIn>;

def CoreFoundation : Package<"coreFoundation">, ParentPackage<OSX>;
def Containers : Package<"containers">, ParentPackage<CoreFoundation>;

def LocalizabilityAlpha : Package<"localizability">, ParentPackage<CocoaAlpha>;
def LocalizabilityOptIn : Package<"localizability">, ParentPackage<CocoaOptIn>;

def MPI : Package<"mpi">, ParentPackage<OptIn>;

def LLVM : Package<"llvm">;
def LLVMAlpha : Package<"llvm">, ParentPackage<Alpha>;

// The APIModeling package is for checkers that model APIs and don't perform
// any diagnostics. These checkers are always turned on; this package is
// intended for API modeling that is not controlled by the target triple.
def APIModeling : Package<"apiModeling">;
def GoogleAPIModeling : Package<"google">, ParentPackage<APIModeling>;

def Debug : Package<"debug">;

def CloneDetectionAlpha : Package<"clone">, ParentPackage<Alpha>;

//===----------------------------------------------------------------------===//
// Core Checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Core in {

def DereferenceChecker : Checker<"NullDereference">,
  HelpText<"Check for dereferences of null pointers">,
  Documentation<HasDocumentation>;

def CallAndMessageChecker : Checker<"CallAndMessage">,
  HelpText<"Check for logical errors for function calls and Objective-C "
           "message expressions (e.g., uninitialized arguments, null function "
           "pointers)">,
  Documentation<HasDocumentation>;

def NonNullParamChecker : Checker<"NonNullParamChecker">,
  HelpText<"Check for null pointers passed as arguments to a function whose "
           "arguments are references or marked with the 'nonnull' attribute">,
  Documentation<HasDocumentation>;

def VLASizeChecker : Checker<"VLASize">,
  HelpText<"Check for declarations of VLA of undefined or zero size">,
  Documentation<HasDocumentation>;

def DivZeroChecker : Checker<"DivideZero">,
  HelpText<"Check for division by zero">,
  Documentation<HasDocumentation>;

def UndefResultChecker : Checker<"UndefinedBinaryOperatorResult">,
  HelpText<"Check for undefined results of binary operators">,
  Documentation<HasDocumentation>;

def StackAddrEscapeChecker : Checker<"StackAddressEscape">,
  HelpText<"Check that addresses to stack memory do not escape the function">,
  Documentation<HasDocumentation>;

def DynamicTypePropagation : Checker<"DynamicTypePropagation">,
  HelpText<"Generate dynamic type information">,
  Documentation<NotDocumented>;

def NonnullGlobalConstantsChecker: Checker<"NonnilStringConstants">,
  HelpText<"Assume that const string-like globals are non-null">,
  Documentation<NotDocumented>;

} // end "core"

let ParentPackage = CoreAlpha in {

def BoolAssignmentChecker : Checker<"BoolAssignment">,
  HelpText<"Warn about assigning non-{0,1} values to Boolean variables">,
  Documentation<HasAlphaDocumentation>;

def CastSizeChecker : Checker<"CastSize">,
  HelpText<"Check when casting a malloc'ed type T, whether the size is a "
           "multiple of the size of T">,
  Documentation<HasAlphaDocumentation>;

def CastToStructChecker : Checker<"CastToStruct">,
  HelpText<"Check for cast from non-struct pointer to struct pointer">,
  Documentation<HasAlphaDocumentation>;

def ConversionChecker : Checker<"Conversion">,
  HelpText<"Loss of sign/precision in implicit conversions">,
  Documentation<HasAlphaDocumentation>;

def IdenticalExprChecker : Checker<"IdenticalExpr">,
  HelpText<"Warn about unintended use of identical expressions in operators">,
  Documentation<HasAlphaDocumentation>;

def FixedAddressChecker : Checker<"FixedAddr">,
  HelpText<"Check for assignment of a fixed address to a pointer">,
  Documentation<HasAlphaDocumentation>;

def PointerArithChecker : Checker<"PointerArithm">,
  HelpText<"Check for pointer arithmetic on locations other than array "
           "elements">,
  Documentation<HasAlphaDocumentation>;

def PointerSubChecker : Checker<"PointerSub">,
  HelpText<"Check for pointer subtractions on two pointers pointing to "
           "different memory chunks">,
  Documentation<HasAlphaDocumentation>;

def SizeofPointerChecker : Checker<"SizeofPtr">,
  HelpText<"Warn about unintended use of sizeof() on pointer expressions">,
  Documentation<HasAlphaDocumentation>;

def CallAndMessageUnInitRefArg : Checker<"CallAndMessageUnInitRefArg">,
  HelpText<"Check for logical errors for function calls and Objective-C "
           "message expressions (e.g., uninitialized arguments, null function "
           "pointers, and pointer to undefined variables)">,
  Documentation<HasAlphaDocumentation>;

def TestAfterDivZeroChecker : Checker<"TestAfterDivZero">,
  HelpText<"Check for division by variable that is later compared against 0. "
           "Either the comparison is useless or there is division by zero.">,
  Documentation<HasAlphaDocumentation>;

def DynamicTypeChecker : Checker<"DynamicTypeChecker">,
  HelpText<"Check for cases where the dynamic and the static type of an object "
           "are unrelated.">,
  Documentation<HasAlphaDocumentation>;

def StackAddrAsyncEscapeChecker : Checker<"StackAddressAsyncEscape">,
  HelpText<"Check that addresses to stack memory do not escape the function">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.core"

let ParentPackage = Nullability in {

def NullPassedToNonnullChecker : Checker<"NullPassedToNonnull">,
  HelpText<"Warns when a null pointer is passed to a pointer which has a "
           "_Nonnull type.">,
  Documentation<HasDocumentation>;

def NullReturnedFromNonnullChecker : Checker<"NullReturnedFromNonnull">,
  HelpText<"Warns when a null pointer is returned from a function that has "
           "_Nonnull return type.">,
  Documentation<HasDocumentation>;

def NullableDereferencedChecker : Checker<"NullableDereferenced">,
  HelpText<"Warns when a nullable pointer is dereferenced.">,
  Documentation<HasDocumentation>;

def NullablePassedToNonnullChecker : Checker<"NullablePassedToNonnull">,
  HelpText<"Warns when a nullable pointer is passed to a pointer which has a "
           "_Nonnull type.">,
  Documentation<HasDocumentation>;

def NullableReturnedFromNonnullChecker : Checker<"NullableReturnedFromNonnull">,
  HelpText<"Warns when a nullable pointer is returned from a function that has "
           "_Nonnull return type.">,
  Documentation<NotDocumented>;

} // end "nullability"

let ParentPackage = APIModeling in {

def StdCLibraryFunctionsChecker : Checker<"StdCLibraryFunctions">,
  HelpText<"Improve modeling of the C standard library functions">,
  Documentation<NotDocumented>;

def TrustNonnullChecker : Checker<"TrustNonnull">,
  HelpText<"Trust that returns from framework methods annotated with _Nonnull "
           "are not null">,
  Documentation<NotDocumented>;

} // end "apiModeling"

//===----------------------------------------------------------------------===//
// Evaluate "builtin" functions.
//===----------------------------------------------------------------------===//

let ParentPackage = CoreBuiltin in {

def NoReturnFunctionChecker : Checker<"NoReturnFunctions">,
  HelpText<"Evaluate \"panic\" functions that are known to not return to the "
           "caller">,
  Documentation<NotDocumented>;

def BuiltinFunctionChecker : Checker<"BuiltinFunctions">,
  HelpText<"Evaluate compiler builtin functions (e.g., alloca())">,
  Documentation<NotDocumented>;

} // end "core.builtin"

//===----------------------------------------------------------------------===//
// Uninitialized values checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = CoreUninitialized in {

def UndefinedArraySubscriptChecker : Checker<"ArraySubscript">,
  HelpText<"Check for uninitialized values used as array subscripts">,
  Documentation<HasDocumentation>;

def UndefinedAssignmentChecker : Checker<"Assign">,
  HelpText<"Check for assigning uninitialized values">,
  Documentation<HasDocumentation>;

def UndefBranchChecker : Checker<"Branch">,
  HelpText<"Check for uninitialized values used as branch conditions">,
  Documentation<HasDocumentation>;

def UndefCapturedBlockVarChecker : Checker<"CapturedBlockVariable">,
  HelpText<"Check for blocks that capture uninitialized values">,
  Documentation<NotDocumented>;

def ReturnUndefChecker : Checker<"UndefReturn">,
  HelpText<"Check for uninitialized values being returned to the caller">,
  Documentation<HasDocumentation>;

} // end "core.uninitialized"

//===----------------------------------------------------------------------===//
// C++ checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Cplusplus in {

def InnerPointerChecker : Checker<"InnerPointer">,
  HelpText<"Check for inner pointers of C++ containers used after "
           "re/deallocation">,
  Documentation<NotDocumented>;

def NewDeleteChecker : Checker<"NewDelete">,
  HelpText<"Check for double-free and use-after-free problems. Traces memory "
           "managed by new/delete.">,
  Documentation<HasDocumentation>;

def NewDeleteLeaksChecker : Checker<"NewDeleteLeaks">,
  HelpText<"Check for memory leaks. Traces memory managed by new/delete.">,
  Documentation<HasDocumentation>;

def CXXSelfAssignmentChecker : Checker<"SelfAssignment">,
  HelpText<"Checks C++ copy and move assignment operators for self assignment">,
  Documentation<NotDocumented>;

def MoveChecker: Checker<"Move">,
   HelpText<"Find use-after-move bugs in C++">,
  Documentation<HasDocumentation>;

} // end: "cplusplus"

let ParentPackage = CplusplusOptIn in {

def VirtualCallChecker : Checker<"VirtualCall">,
  HelpText<"Check virtual function calls during construction or destruction">,
  Documentation<HasDocumentation>;

} // end: "optin.cplusplus"

let ParentPackage = CplusplusAlpha in {

def DeleteWithNonVirtualDtorChecker : Checker<"DeleteWithNonVirtualDtor">,
  HelpText<"Reports destructions of polymorphic objects with a non-virtual "
           "destructor in their base class">,
  Documentation<HasAlphaDocumentation>;

def EnumCastOutOfRangeChecker : Checker<"EnumCastOutOfRange">,
  HelpText<"Check integer to enumeration casts for out of range values">,
  Documentation<HasAlphaDocumentation>;

def InvalidatedIteratorChecker : Checker<"InvalidatedIterator">,
  HelpText<"Check for use of invalidated iterators">,
  Documentation<HasAlphaDocumentation>;

def IteratorRangeChecker : Checker<"IteratorRange">,
  HelpText<"Check for iterators used outside their valid ranges">,
  Documentation<HasAlphaDocumentation>;

def MismatchedIteratorChecker : Checker<"MismatchedIterator">,
  HelpText<"Check for use of iterators of different containers where iterators "
           "of the same container are expected">,
  Documentation<HasAlphaDocumentation>;

def UninitializedObjectChecker: Checker<"UninitializedObject">,
     HelpText<"Reports uninitialized fields after object construction">,
  Documentation<HasAlphaDocumentation>;

} // end: "alpha.cplusplus"


//===----------------------------------------------------------------------===//
// Valist checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Valist in {

def UninitializedChecker : Checker<"Uninitialized">,
  HelpText<"Check for usages of uninitialized (or already released) va_lists.">,
  Documentation<NotDocumented>;

def UnterminatedChecker : Checker<"Unterminated">,
  HelpText<"Check for va_lists which are not released by a va_end call.">,
  Documentation<NotDocumented>;

def CopyToSelfChecker : Checker<"CopyToSelf">,
  HelpText<"Check for va_lists which are copied onto itself.">,
  Documentation<NotDocumented>;

} // end : "valist"

//===----------------------------------------------------------------------===//
// Deadcode checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = DeadCode in {

def DeadStoresChecker : Checker<"DeadStores">,
  HelpText<"Check for values stored to variables that are never read "
           "afterwards">,
  Documentation<HasDocumentation>;

} // end DeadCode

let ParentPackage = DeadCodeAlpha in {

def UnreachableCodeChecker : Checker<"UnreachableCode">,
  HelpText<"Check unreachable code">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.deadcode"

//===----------------------------------------------------------------------===//
// Performance checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Performance in {

def PaddingChecker : Checker<"Padding">,
  HelpText<"Check for excessively padded structs.">,
  Documentation<NotDocumented>;

} // end: "padding"

//===----------------------------------------------------------------------===//
// Security checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = InsecureAPI in {

def bcmp : Checker<"bcmp">,
  HelpText<"Warn on uses of the 'bcmp' function">,
  Documentation<HasDocumentation>;
def bcopy : Checker<"bcopy">,
  HelpText<"Warn on uses of the 'bcopy' function">,
  Documentation<HasDocumentation>;
def bzero : Checker<"bzero">,
  HelpText<"Warn on uses of the 'bzero' function">,
  Documentation<HasDocumentation>;
def gets : Checker<"gets">,
  HelpText<"Warn on uses of the 'gets' function">,
  Documentation<HasDocumentation>;
def getpw : Checker<"getpw">,
  HelpText<"Warn on uses of the 'getpw' function">,
  Documentation<HasDocumentation>;
def mktemp : Checker<"mktemp">,
  HelpText<"Warn on uses of the 'mktemp' function">,
  Documentation<HasDocumentation>;
def mkstemp : Checker<"mkstemp">,
  HelpText<"Warn when 'mkstemp' is passed fewer than 6 X's in the format "
           "string">,
  Documentation<HasDocumentation>;
def rand : Checker<"rand">,
  HelpText<"Warn on uses of the 'rand', 'random', and related functions">,
  Documentation<HasDocumentation>;
def strcpy : Checker<"strcpy">,
  HelpText<"Warn on uses of the 'strcpy' and 'strcat' functions">,
  Documentation<HasDocumentation>;
def vfork : Checker<"vfork">,
  HelpText<"Warn on uses of the 'vfork' function">,
  Documentation<HasDocumentation>;
def UncheckedReturn : Checker<"UncheckedReturn">,
  HelpText<"Warn on uses of functions whose return values must be always "
           "checked">,
  Documentation<HasDocumentation>;

} // end "security.insecureAPI"

let ParentPackage = Security in {

def FloatLoopCounter : Checker<"FloatLoopCounter">,
  HelpText<"Warn on using a floating point value as a loop counter (CERT: "
           "FLP30-C, FLP30-CPP)">,
  Documentation<HasDocumentation>;

} // end "security"

let ParentPackage = SecurityAlpha in {

def ArrayBoundChecker : Checker<"ArrayBound">,
  HelpText<"Warn about buffer overflows (older checker)">,
  Documentation<HasAlphaDocumentation>;

def ArrayBoundCheckerV2 : Checker<"ArrayBoundV2">,
  HelpText<"Warn about buffer overflows (newer checker)">,
  Documentation<HasAlphaDocumentation>;

def ReturnPointerRangeChecker : Checker<"ReturnPtrRange">,
  HelpText<"Check for an out-of-bound pointer being returned to callers">,
  Documentation<HasAlphaDocumentation>;

def MallocOverflowSecurityChecker : Checker<"MallocOverflow">,
  HelpText<"Check for overflows in the arguments to malloc()">,
  Documentation<HasAlphaDocumentation>;

// Operating systems specific PROT_READ/PROT_WRITE values is not implemented,
// the defaults are correct for several common operating systems though,
// but may need to be overridden via the related analyzer-config flags.
def MmapWriteExecChecker : Checker<"MmapWriteExec">,
  HelpText<"Warn on mmap() calls that are both writable and executable">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.security"

//===----------------------------------------------------------------------===//
// Taint checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Taint in {

def GenericTaintChecker : Checker<"TaintPropagation">,
  HelpText<"Generate taint information used by other checkers">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.security.taint"

//===----------------------------------------------------------------------===//
// Unix API checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = Unix in {

def UnixAPIMisuseChecker : Checker<"API">,
  HelpText<"Check calls to various UNIX/Posix functions">,
  Documentation<HasDocumentation>;

def MallocChecker: Checker<"Malloc">,
  HelpText<"Check for memory leaks, double free, and use-after-free problems. "
           "Traces memory managed by malloc()/free().">,
  Documentation<HasDocumentation>;

def MallocSizeofChecker : Checker<"MallocSizeof">,
  HelpText<"Check for dubious malloc arguments involving sizeof">,
  Documentation<HasDocumentation>;

def MismatchedDeallocatorChecker : Checker<"MismatchedDeallocator">,
  HelpText<"Check for mismatched deallocators.">,
  Documentation<HasDocumentation>;

def VforkChecker : Checker<"Vfork">,
  HelpText<"Check for proper usage of vfork">,
  Documentation<HasDocumentation>;

} // end "unix"

let ParentPackage = UnixAlpha in {

def ChrootChecker : Checker<"Chroot">,
  HelpText<"Check improper use of chroot">,
  Documentation<HasAlphaDocumentation>;

def PthreadLockChecker : Checker<"PthreadLock">,
  HelpText<"Simple lock -> unlock checker">,
  Documentation<HasAlphaDocumentation>;

def StreamChecker : Checker<"Stream">,
  HelpText<"Check stream handling functions">,
  Documentation<HasAlphaDocumentation>;

def SimpleStreamChecker : Checker<"SimpleStream">,
  HelpText<"Check for misuses of stream APIs">,
  Documentation<HasAlphaDocumentation>;

def BlockInCriticalSectionChecker : Checker<"BlockInCriticalSection">,
  HelpText<"Check for calls to blocking functions inside a critical section">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.unix"

let ParentPackage = CString in {

def CStringNullArg : Checker<"NullArg">,
  HelpText<"Check for null pointers being passed as arguments to C string "
           "functions">,
  Documentation<HasDocumentation>;

def CStringSyntaxChecker : Checker<"BadSizeArg">,
  HelpText<"Check the size argument passed into C string functions for common "
           "erroneous patterns">,
  Documentation<HasDocumentation>;

} // end "unix.cstring"

let ParentPackage = CStringAlpha in {

def CStringOutOfBounds : Checker<"OutOfBounds">,
  HelpText<"Check for out-of-bounds access in string functions">,
  Documentation<HasAlphaDocumentation>;

def CStringBufferOverlap : Checker<"BufferOverlap">,
  HelpText<"Checks for overlap in two buffer arguments">,
  Documentation<HasAlphaDocumentation>;

def CStringNotNullTerm : Checker<"NotNullTerminated">,
  HelpText<"Check for arguments which are not null-terminating strings">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.unix.cstring"

//===----------------------------------------------------------------------===//
// Mac OS X, Cocoa, and Core Foundation checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = OSX in {

def NumberObjectConversionChecker : Checker<"NumberObjectConversion">,
  HelpText<"Check for erroneous conversions of objects representing numbers "
           "into numbers">,
  Documentation<NotDocumented>;

def MacOSXAPIChecker : Checker<"API">,
  HelpText<"Check for proper uses of various Apple APIs">,
  Documentation<HasDocumentation>;

def MacOSKeychainAPIChecker : Checker<"SecKeychainAPI">,
  HelpText<"Check for proper uses of Secure Keychain APIs">,
  Documentation<HasDocumentation>;

def ObjCPropertyChecker : Checker<"ObjCProperty">,
  HelpText<"Check for proper uses of Objective-C properties">,
  Documentation<NotDocumented>;

def OSObjectRetainCountChecker : Checker<"OSObjectRetainCount">,
  HelpText<"Check for leaks and improper reference count management for OSObject">,
  Documentation<NotDocumented>;

} // end "osx"

let ParentPackage = Cocoa in {

def RunLoopAutoreleaseLeakChecker : Checker<"RunLoopAutoreleaseLeak">,
  HelpText<"Check for leaked memory in autorelease pools that will never be "
           "drained">,
  Documentation<NotDocumented>;

def ObjCAtSyncChecker : Checker<"AtSync">,
  HelpText<"Check for nil pointers used as mutexes for @synchronized">,
  Documentation<HasDocumentation>;

def NilArgChecker : Checker<"NilArg">,
  HelpText<"Check for prohibited nil arguments to ObjC method calls">,
  Documentation<HasDocumentation>;

def ClassReleaseChecker : Checker<"ClassRelease">,
  HelpText<"Check for sending 'retain', 'release', or 'autorelease' directly "
           "to a Class">,
  Documentation<HasDocumentation>;

def VariadicMethodTypeChecker : Checker<"VariadicMethodTypes">,
  HelpText<"Check for passing non-Objective-C types to variadic collection "
           "initialization methods that expect only Objective-C types">,
  Documentation<HasDocumentation>;

def NSAutoreleasePoolChecker : Checker<"NSAutoreleasePool">,
  HelpText<"Warn for suboptimal uses of NSAutoreleasePool in Objective-C GC "
           "mode">,
  Documentation<HasDocumentation>;

def ObjCMethSigsChecker : Checker<"IncompatibleMethodTypes">,
  HelpText<"Warn about Objective-C method signatures with type "
           "incompatibilities">,
  Documentation<HasDocumentation>;

def ObjCUnusedIvarsChecker : Checker<"UnusedIvars">,
  HelpText<"Warn about private ivars that are never used">,
  Documentation<HasDocumentation>;

def ObjCSelfInitChecker : Checker<"SelfInit">,
  HelpText<"Check that 'self' is properly initialized inside an initializer "
           "method">,
  Documentation<HasDocumentation>;

def ObjCLoopChecker : Checker<"Loops">,
  HelpText<"Improved modeling of loops using Cocoa collection types">,
  Documentation<NotDocumented>;

def ObjCNonNilReturnValueChecker : Checker<"NonNilReturnValue">,
  HelpText<"Model the APIs that are guaranteed to return a non-nil value">,
  Documentation<NotDocumented>;

def ObjCSuperCallChecker : Checker<"MissingSuperCall">,
  HelpText<"Warn about Objective-C methods that lack a necessary call to "
           "super">,
  Documentation<NotDocumented>;

def NSErrorChecker : Checker<"NSError">,
  HelpText<"Check usage of NSError** parameters">,
  Documentation<HasDocumentation>;

def RetainCountChecker : Checker<"RetainCount">,
  HelpText<"Check for leaks and improper reference count management">,
  Documentation<HasDocumentation>;

def ObjCGenericsChecker : Checker<"ObjCGenerics">,
  HelpText<"Check for type errors when using Objective-C generics">,
  Documentation<HasDocumentation>;

def ObjCDeallocChecker : Checker<"Dealloc">,
  HelpText<"Warn about Objective-C classes that lack a correct implementation "
           "of -dealloc">,
  Documentation<HasDocumentation>;

def ObjCSuperDeallocChecker : Checker<"SuperDealloc">,
  HelpText<"Warn about improper use of '[super dealloc]' in Objective-C">,
  Documentation<HasDocumentation>;

def AutoreleaseWriteChecker : Checker<"AutoreleaseWrite">,
  HelpText<"Warn about potentially crashing writes to autoreleasing objects "
           "from different autoreleasing pools in Objective-C">,
  Documentation<NotDocumented>;

} // end "osx.cocoa"

let ParentPackage = Performance in {

def GCDAntipattern : Checker<"GCDAntipattern">,
  HelpText<"Check for performance anti-patterns when using Grand Central "
           "Dispatch">,
  Documentation<NotDocumented>;
} // end "optin.performance"

let ParentPackage = CocoaAlpha in {

def InstanceVariableInvalidation : Checker<"InstanceVariableInvalidation">,
  HelpText<"Check that the invalidatable instance variables are invalidated in "
           "the methods annotated with objc_instance_variable_invalidator">,
  Documentation<HasAlphaDocumentation>;

def MissingInvalidationMethod : Checker<"MissingInvalidationMethod">,
  HelpText<"Check that the invalidation methods are present in classes that "
           "contain invalidatable instance variables">,
  Documentation<HasAlphaDocumentation>;

def DirectIvarAssignment : Checker<"DirectIvarAssignment">,
  HelpText<"Check for direct assignments to instance variables">,
  Documentation<HasAlphaDocumentation>;

def DirectIvarAssignmentForAnnotatedFunctions :
  Checker<"DirectIvarAssignmentForAnnotatedFunctions">,
  HelpText<"Check for direct assignments to instance variables in the methods "
           "annotated with objc_no_direct_instance_variable_assignment">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.osx.cocoa"

let ParentPackage = CoreFoundation in {

def CFNumberChecker : Checker<"CFNumber">,
  HelpText<"Check for proper uses of CFNumber APIs">,
  Documentation<HasDocumentation>;

def CFRetainReleaseChecker : Checker<"CFRetainRelease">,
  HelpText<"Check for null arguments to CFRetain/CFRelease/CFMakeCollectable">,
  Documentation<HasDocumentation>;

def CFErrorChecker : Checker<"CFError">,
  HelpText<"Check usage of CFErrorRef* parameters">,
  Documentation<HasDocumentation>;

} // end "osx.coreFoundation"

let ParentPackage = Containers in {

def ObjCContainersASTChecker : Checker<"PointerSizedValues">,
  HelpText<"Warns if 'CFArray', 'CFDictionary', 'CFSet' are created with "
           "non-pointer-size values">,
  Documentation<HasDocumentation>;

def ObjCContainersChecker : Checker<"OutOfBounds">,
  HelpText<"Checks for index out-of-bounds when using 'CFArray' API">,
  Documentation<HasDocumentation>;

} // end "osx.coreFoundation.containers"

let ParentPackage = LocalizabilityOptIn in {

def NonLocalizedStringChecker : Checker<"NonLocalizedStringChecker">,
  HelpText<"Warns about uses of non-localized NSStrings passed to UI methods "
           "expecting localized NSStrings">,
  Documentation<HasDocumentation>;

def EmptyLocalizationContextChecker :
  Checker<"EmptyLocalizationContextChecker">,
  HelpText<"Check that NSLocalizedString macros include a comment for context">,
  Documentation<HasDocumentation>;

} // end "optin.osx.cocoa.localizability"

let ParentPackage = LocalizabilityAlpha in {

def PluralMisuseChecker : Checker<"PluralMisuseChecker">,
  HelpText<"Warns against using one vs. many plural pattern in code when "
           "generating localized strings.">,
  Documentation<HasAlphaDocumentation>;

} // end "alpha.osx.cocoa.localizability"

let ParentPackage = MPI in {

def MPIChecker : Checker<"MPI-Checker">,
  HelpText<"Checks MPI code">,
  Documentation<HasDocumentation>;

} // end "optin.mpi"

//===----------------------------------------------------------------------===//
// Checkers for LLVM development.
//===----------------------------------------------------------------------===//

let ParentPackage = LLVMAlpha in {

def LLVMConventionsChecker : Checker<"Conventions">,
  HelpText<"Check code for LLVM codebase conventions">,
  Documentation<HasAlphaDocumentation>;

} // end "llvm"

//===----------------------------------------------------------------------===//
// Checkers modeling Google APIs.
//===----------------------------------------------------------------------===//

let ParentPackage = GoogleAPIModeling in {

def GTestChecker : Checker<"GTest">,
  HelpText<"Model gtest assertion APIs">,
  Documentation<NotDocumented>;

} // end "apiModeling.google"

//===----------------------------------------------------------------------===//
// Debugging checkers (for analyzer development).
//===----------------------------------------------------------------------===//

let ParentPackage = Debug in {

def AnalysisOrderChecker : Checker<"AnalysisOrder">,
  HelpText<"Print callbacks that are called during analysis in order">,
  Documentation<NotDocumented>;

def DominatorsTreeDumper : Checker<"DumpDominators">,
  HelpText<"Print the dominance tree for a given CFG">,
  Documentation<NotDocumented>;

def LiveVariablesDumper : Checker<"DumpLiveVars">,
  HelpText<"Print results of live variable analysis">,
  Documentation<NotDocumented>;

def LiveStatementsDumper : Checker<"DumpLiveStmts">,
  HelpText<"Print results of live statement analysis">,
  Documentation<NotDocumented>;

def CFGViewer : Checker<"ViewCFG">,
  HelpText<"View Control-Flow Graphs using GraphViz">,
  Documentation<NotDocumented>;

def CFGDumper : Checker<"DumpCFG">,
  HelpText<"Display Control-Flow Graphs">,
  Documentation<NotDocumented>;

def CallGraphViewer : Checker<"ViewCallGraph">,
  HelpText<"View Call Graph using GraphViz">,
  Documentation<NotDocumented>;

def CallGraphDumper : Checker<"DumpCallGraph">,
  HelpText<"Display Call Graph">,
  Documentation<NotDocumented>;

def ConfigDumper : Checker<"ConfigDumper">,
  HelpText<"Dump config table">,
  Documentation<NotDocumented>;

def TraversalDumper : Checker<"DumpTraversal">,
  HelpText<"Print branch conditions as they are traversed by the engine">,
  Documentation<NotDocumented>;

def CallDumper : Checker<"DumpCalls">,
  HelpText<"Print calls as they are traversed by the engine">,
  Documentation<NotDocumented>;

def AnalyzerStatsChecker : Checker<"Stats">,
  HelpText<"Emit warnings with analyzer statistics">,
  Documentation<NotDocumented>;

def TaintTesterChecker : Checker<"TaintTest">,
  HelpText<"Mark tainted symbols as such.">,
  Documentation<NotDocumented>;

def ExprInspectionChecker : Checker<"ExprInspection">,
  HelpText<"Check the analyzer's understanding of expressions">,
  Documentation<NotDocumented>;

def ExplodedGraphViewer : Checker<"ViewExplodedGraph">,
  HelpText<"View Exploded Graphs using GraphViz">,
  Documentation<NotDocumented>;

} // end "debug"


//===----------------------------------------------------------------------===//
// Clone Detection
//===----------------------------------------------------------------------===//

let ParentPackage = CloneDetectionAlpha in {

def CloneChecker : Checker<"CloneChecker">,
  HelpText<"Reports similar pieces of code.">,
  Documentation<HasAlphaDocumentation>;

} // end "clone"

//===----------------------------------------------------------------------===//
// Portability checkers.
//===----------------------------------------------------------------------===//

let ParentPackage = PortabilityOptIn in {

def UnixAPIPortabilityChecker : Checker<"UnixAPI">,
  HelpText<"Finds implementation-defined behavior in UNIX/Posix functions">,
  Documentation<NotDocumented>;

} // end optin.portability
