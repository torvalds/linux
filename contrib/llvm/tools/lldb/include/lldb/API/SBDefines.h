//===-- SBDefines.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBDefines_h_
#define LLDB_SBDefines_h_


#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "lldb/lldb-versioning.h"

#ifdef SWIG
#define LLDB_API
#endif

// Forward Declarations
namespace lldb {

class LLDB_API SBAddress;
class LLDB_API SBBlock;
class LLDB_API SBBreakpoint;
class LLDB_API SBBreakpointLocation;
class LLDB_API SBBreakpointName;
class LLDB_API SBBreakpointNameImpl;
class LLDB_API SBBroadcaster;
class LLDB_API SBCommand;
class LLDB_API SBCommandInterpreter;
class LLDB_API SBCommandInterpreterRunOptions;
class LLDB_API SBCommandPluginInterface;
class LLDB_API SBCommandReturnObject;
class LLDB_API SBCommunication;
class LLDB_API SBCompileUnit;
class LLDB_API SBData;
class LLDB_API SBDebugger;
class LLDB_API SBDeclaration;
class LLDB_API SBError;
class LLDB_API SBEvent;
class LLDB_API SBEventList;
class LLDB_API SBExecutionContext;
class LLDB_API SBExpressionOptions;
class LLDB_API SBFileSpec;
class LLDB_API SBFileSpecList;
class LLDB_API SBFrame;
class LLDB_API SBFunction;
class LLDB_API SBHostOS;
class LLDB_API SBInitializerOptions;
class LLDB_API SBInstruction;
class LLDB_API SBInstructionList;
class LLDB_API SBLanguageRuntime;
class LLDB_API SBLaunchInfo;
class LLDB_API SBLineEntry;
class LLDB_API SBListener;
class LLDB_API SBMemoryRegionInfo;
class LLDB_API SBMemoryRegionInfoList;
class LLDB_API SBModule;
class LLDB_API SBModuleSpec;
class LLDB_API SBModuleSpecList;
class LLDB_API SBProcess;
class LLDB_API SBProcessInfo;
class LLDB_API SBQueue;
class LLDB_API SBQueueItem;
class LLDB_API SBSection;
class LLDB_API SBSourceManager;
class LLDB_API SBStream;
class LLDB_API SBStringList;
class LLDB_API SBStructuredData;
class LLDB_API SBSymbol;
class LLDB_API SBSymbolContext;
class LLDB_API SBSymbolContextList;
class LLDB_API SBTarget;
class LLDB_API SBThread;
class LLDB_API SBThreadCollection;
class LLDB_API SBThreadPlan;
class LLDB_API SBTrace;
class LLDB_API SBTraceOptions;
class LLDB_API SBType;
class LLDB_API SBTypeCategory;
class LLDB_API SBTypeEnumMember;
class LLDB_API SBTypeEnumMemberList;
class LLDB_API SBTypeFilter;
class LLDB_API SBTypeFormat;
class LLDB_API SBTypeMemberFunction;
class LLDB_API SBTypeNameSpecifier;
class LLDB_API SBTypeSummary;
class LLDB_API SBTypeSummaryOptions;
class LLDB_API SBTypeSynthetic;
class LLDB_API SBTypeList;
class LLDB_API SBValue;
class LLDB_API SBValueList;
class LLDB_API SBVariablesOptions;
class LLDB_API SBWatchpoint;
class LLDB_API SBUnixSignals;

typedef bool (*SBBreakpointHitCallback)(void *baton, SBProcess &process,
                                        SBThread &thread,
                                        lldb::SBBreakpointLocation &location);
}

#endif // LLDB_SBDefines_h_
