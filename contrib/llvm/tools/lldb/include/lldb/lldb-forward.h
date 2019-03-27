//===-- lldb-forward.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_lldb_forward_h_
#define LLDB_lldb_forward_h_

#if defined(__cplusplus)

#include "lldb/Utility/SharingPtr.h"

//----------------------------------------------------------------------
// lldb forward declarations
//----------------------------------------------------------------------
namespace lldb_private {

class ABI;
class Address;
class AddressImpl;
class AddressRange;
class AddressResolver;
class ArchSpec;
class ArmUnwindInfo;
class Args;
class ASTResultSynthesizer;
class ASTStructExtractor;
class Baton;
class Block;
class Breakpoint;
class BreakpointID;
class BreakpointIDList;
class BreakpointList;
class BreakpointLocation;
class BreakpointLocationCollection;
class BreakpointLocationList;
class BreakpointName;
class BreakpointOptionGroup;
class BreakpointOptions;
class BreakpointResolver;
class BreakpointSite;
class BreakpointSiteList;
class BroadcastEventSpec;
class Broadcaster;
class BroadcasterManager;
class CPPLanguageRuntime;
class ClangASTContext;
class ClangASTImporter;
class ClangASTMetadata;
class ClangASTSource;
class ClangExpressionDeclMap;
class ClangExpressionParser;
class ClangExpressionVariable;
class ClangExpressionVariables;
class ClangModulesDeclVendor;
class ClangPersistentVariables;
class CommandInterpreter;
class CommandInterpreterRunOptions;
class CommandObject;
class CommandObjectMultiword;
class CommandReturnObject;
class Communication;
class CompactUnwindInfo;
struct CompilerContext;
class CompilerDecl;
class CompilerDeclContext;
class CompilerType;
class CompileUnit;
class Condition;
class Connection;
class ConnectionFileDescriptor;
class ConstString;
class CXXSyntheticChildren;
class DWARFCallFrameInfo;
class DWARFDataExtractor;
class DWARFExpression;
class DataBuffer;
class DataEncoder;
class DataExtractor;
class Debugger;
class Declaration;
class DiagnosticManager;
class Disassembler;
class DumpValueObjectOptions;
class DynamicCheckerFunctions;
class DynamicLoader;
class Editline;
class EmulateInstruction;
class Status;
class EvaluateExpressionOptions;
class Event;
class EventData;
class EventDataStructuredData;
class ExecutionContext;
class ExecutionContextRef;
class ExecutionContextRefLocker;
class ExecutionContextScope;
class Expression;
class ExpressionVariable;
class ExpressionVariableList;
class ExpressionTypeSystemHelper;
class File;
class FileSpec;
class FileSpecList;
class Flags;
class TypeCategoryImpl;
class FormatManager;
class FormattersMatchCandidate;
class FuncUnwinders;
class Function;
class FunctionInfo;
class InlineFunctionInfo;
class Instruction;
class InstructionList;
class InstrumentationRuntime;
class IOHandler;
class IOObject;
class IRExecutionUnit;
class JITLoader;
class JITLoaderList;
class Language;
class LanguageCategory;
class LanguageRuntime;
class LineTable;
class Listener;
class Log;
class Mangled;
class Materializer;
class MemoryHistory;
class MemoryRegionInfo;
class MemoryRegionInfos;
class Module;
class ModuleList;
class ModuleSpec;
class ModuleSpecList;
class Mutex;
struct NameSearchContext;
class ObjCLanguageRuntime;
class ObjCRuntimeSyntheticProvider;
class ObjectContainer;
class OptionGroup;
class OptionGroupOptions;
class OptionGroupPlatform;
class ObjectFile;
class ObjectFileJITDelegate;
class OperatingSystem;
class Options;
class OptionValue;
class OptionValueArch;
class OptionValueArgs;
class OptionValueArray;
class OptionValueBoolean;
class OptionValueChar;
class OptionValueDictionary;
class OptionValueEnumeration;
class OptionValueFileSpec;
class OptionValueFileSpecList;
class OptionValueFormat;
class OptionValueLanguage;
class OptionValueFormatEntity;
class OptionValuePathMappings;
class OptionValueProperties;
class OptionValueRegex;
class OptionValueSInt64;
class OptionValueString;
class OptionValueUInt64;
class OptionValueUUID;
class NamedOption;
class PathMappingList;
class FunctionCaller;
class PersistentExpressionState;
class Platform;
class Process;
class ProcessAttachInfo;
class ProcessModID;
class ProcessInfo;
class ProcessInstanceInfo;
class ProcessInstanceInfoList;
class ProcessInstanceInfoMatch;
class ProcessLaunchInfo;
class Property;
struct PropertyDefinition;
class RecognizedStackFrame;
class RegisterCheckpoint;
class RegisterContext;
class RegisterLocation;
class RegisterLocationList;
class RegisterValue;
class RegularExpression;
class REPL;
class RichManglingContext;
class Scalar;
class ScriptInterpreter;
class ScriptInterpreterLocker;
struct ScriptSummaryFormat;
class SearchFilter;
class Section;
class SectionImpl;
class SectionList;
class SectionLoadHistory;
class SectionLoadList;
class Settings;
class SourceManager;
class SourceManagerImpl;
class StackFrame;
class StackFrameImpl;
class StackFrameList;
class StackFrameRecognizer;
class StackFrameRecognizerManager;
class StackID;
class StopInfo;
class Stoppoint;
class StoppointCallbackContext;
class StoppointLocation;
class Stream;
template <unsigned N> class StreamBuffer;
class StreamFile;
class StreamString;
class StringList;
struct StringSummaryFormat;
class StructuredDataImpl;
class StructuredDataPlugin;
class SystemRuntime;
class TypeSummaryImpl;
class TypeSummaryOptions;
class Symbol;
class SymbolContext;
class SymbolContextList;
class SymbolContextScope;
class SymbolContextSpecifier;
class SymbolFile;
class SymbolFileType;
class SymbolVendor;
class Symtab;
class SyntheticChildren;
class SyntheticChildrenFrontEnd;
class TypeFilterImpl;
class TypeSystem;
#ifndef LLDB_DISABLE_PYTHON
class ScriptedSyntheticChildren;
#endif
class Queue;
class QueueItem;
class QueueImpl;
class Target;
class TargetList;
class TargetProperties;
class Thread;
class ThreadCollection;
class ThreadList;
class ThreadPlan;
class ThreadPlanBase;
class ThreadPlanRunToAddress;
class ThreadPlanStepInstruction;
class ThreadPlanStepOut;
class ThreadPlanStepOverBreakpoint;
class ThreadPlanStepRange;
class ThreadPlanStepThrough;
class ThreadPlanTracer;
class ThreadSpec;
class TraceOptions;
class Type;
class TypeAndOrName;
class TypeCategoryMap;
class TypeImpl;
class TypeList;
class TypeMap;
class TypeListImpl;
class TypeMemberImpl;
class TypeMemberFunctionImpl;
class TypeEnumMemberImpl;
class TypeEnumMemberListImpl;
class TypeFormatImpl;
class TypeNameSpecifierImpl;
class TypePair;
class TypeValidatorImpl;
class UUID;
class UnixSignals;
class Unwind;
class UnwindAssembly;
class UnwindPlan;
class UnwindTable;
class UserExpression;
class UtilityFunction;
class VMRange;
class Value;
class ValueList;
class ValueObject;
class ValueObjectChild;
class ValueObjectConstResult;
class ValueObjectConstResultChild;
class ValueObjectConstResultImpl;
class ValueObjectList;
class ValueObjectPrinter;
class Variable;
class VariableList;
class Watchpoint;
class WatchpointList;
class WatchpointOptions;
struct LineEntry;

} // namespace lldb_private

//----------------------------------------------------------------------
// lldb forward declarations
//----------------------------------------------------------------------
namespace lldb {

typedef std::shared_ptr<lldb_private::ABI> ABISP;
typedef std::shared_ptr<lldb_private::Baton> BatonSP;
typedef std::shared_ptr<lldb_private::Block> BlockSP;
typedef std::shared_ptr<lldb_private::Breakpoint> BreakpointSP;
typedef std::weak_ptr<lldb_private::Breakpoint> BreakpointWP;
typedef std::shared_ptr<lldb_private::BreakpointSite> BreakpointSiteSP;
typedef std::weak_ptr<lldb_private::BreakpointSite> BreakpointSiteWP;
typedef std::shared_ptr<lldb_private::BreakpointLocation> BreakpointLocationSP;
typedef std::weak_ptr<lldb_private::BreakpointLocation> BreakpointLocationWP;
typedef std::shared_ptr<lldb_private::BreakpointResolver> BreakpointResolverSP;
typedef std::shared_ptr<lldb_private::Broadcaster> BroadcasterSP;
typedef std::shared_ptr<lldb_private::BroadcasterManager> BroadcasterManagerSP;
typedef std::weak_ptr<lldb_private::BroadcasterManager> BroadcasterManagerWP;
typedef std::unique_ptr<lldb_private::ClangASTContext> ClangASTContextUP;
typedef std::shared_ptr<lldb_private::ClangASTImporter> ClangASTImporterSP;
typedef std::unique_ptr<lldb_private::ClangModulesDeclVendor>
    ClangModulesDeclVendorUP;
typedef std::unique_ptr<lldb_private::ClangPersistentVariables>
    ClangPersistentVariablesUP;
typedef std::shared_ptr<lldb_private::UserExpression> UserExpressionSP;
typedef std::shared_ptr<lldb_private::CommandObject> CommandObjectSP;
typedef std::shared_ptr<lldb_private::Communication> CommunicationSP;
typedef std::shared_ptr<lldb_private::Connection> ConnectionSP;
typedef std::shared_ptr<lldb_private::CompileUnit> CompUnitSP;
typedef std::shared_ptr<lldb_private::DataBuffer> DataBufferSP;
typedef std::shared_ptr<lldb_private::DataExtractor> DataExtractorSP;
typedef std::shared_ptr<lldb_private::Debugger> DebuggerSP;
typedef std::weak_ptr<lldb_private::Debugger> DebuggerWP;
typedef std::shared_ptr<lldb_private::Disassembler> DisassemblerSP;
typedef std::unique_ptr<lldb_private::DynamicCheckerFunctions>
    DynamicCheckerFunctionsUP;
typedef std::shared_ptr<lldb_private::DynamicLoader> DynamicLoaderSP;
typedef std::unique_ptr<lldb_private::DynamicLoader> DynamicLoaderUP;
typedef std::shared_ptr<lldb_private::Event> EventSP;
typedef std::shared_ptr<lldb_private::EventData> EventDataSP;
typedef std::shared_ptr<lldb_private::EventDataStructuredData>
    EventDataStructuredDataSP;
typedef std::shared_ptr<lldb_private::ExecutionContextRef>
    ExecutionContextRefSP;
typedef std::shared_ptr<lldb_private::ExpressionVariable> ExpressionVariableSP;
typedef std::shared_ptr<lldb_private::File> FileSP;
typedef std::shared_ptr<lldb_private::Function> FunctionSP;
typedef std::shared_ptr<lldb_private::FunctionCaller> FunctionCallerSP;
typedef std::shared_ptr<lldb_private::FuncUnwinders> FuncUnwindersSP;
typedef std::shared_ptr<lldb_private::InlineFunctionInfo> InlineFunctionInfoSP;
typedef std::shared_ptr<lldb_private::Instruction> InstructionSP;
typedef std::shared_ptr<lldb_private::InstrumentationRuntime>
    InstrumentationRuntimeSP;
typedef std::shared_ptr<lldb_private::IOHandler> IOHandlerSP;
typedef std::shared_ptr<lldb_private::IOObject> IOObjectSP;
typedef std::shared_ptr<lldb_private::IRExecutionUnit> IRExecutionUnitSP;
typedef std::shared_ptr<lldb_private::JITLoader> JITLoaderSP;
typedef std::unique_ptr<lldb_private::JITLoaderList> JITLoaderListUP;
typedef std::shared_ptr<lldb_private::LanguageRuntime> LanguageRuntimeSP;
typedef std::shared_ptr<lldb_private::SystemRuntime> SystemRuntimeSP;
typedef std::unique_ptr<lldb_private::SystemRuntime> SystemRuntimeUP;
typedef std::shared_ptr<lldb_private::LineTable> LineTableSP;
typedef std::shared_ptr<lldb_private::Listener> ListenerSP;
typedef std::weak_ptr<lldb_private::Listener> ListenerWP;
typedef std::shared_ptr<lldb_private::MemoryHistory> MemoryHistorySP;
typedef std::unique_ptr<lldb_private::MemoryRegionInfo> MemoryRegionInfoUP;
typedef std::shared_ptr<lldb_private::Module> ModuleSP;
typedef std::weak_ptr<lldb_private::Module> ModuleWP;
typedef std::shared_ptr<lldb_private::ObjectFile> ObjectFileSP;
typedef std::weak_ptr<lldb_private::ObjectFile> ObjectFileWP;
typedef std::shared_ptr<lldb_private::ObjectFileJITDelegate>
    ObjectFileJITDelegateSP;
typedef std::weak_ptr<lldb_private::ObjectFileJITDelegate>
    ObjectFileJITDelegateWP;
typedef std::unique_ptr<lldb_private::OperatingSystem> OperatingSystemUP;
typedef std::shared_ptr<lldb_private::OptionValue> OptionValueSP;
typedef std::weak_ptr<lldb_private::OptionValue> OptionValueWP;
typedef std::shared_ptr<lldb_private::OptionValueArch> OptionValueArchSP;
typedef std::shared_ptr<lldb_private::OptionValueArgs> OptionValueArgsSP;
typedef std::shared_ptr<lldb_private::OptionValueArray> OptionValueArraySP;
typedef std::shared_ptr<lldb_private::OptionValueBoolean> OptionValueBooleanSP;
typedef std::shared_ptr<lldb_private::OptionValueDictionary>
    OptionValueDictionarySP;
typedef std::shared_ptr<lldb_private::OptionValueFileSpec>
    OptionValueFileSpecSP;
typedef std::shared_ptr<lldb_private::OptionValueFileSpecList>
    OptionValueFileSpecListSP;
typedef std::shared_ptr<lldb_private::OptionValueFormat> OptionValueFormatSP;
typedef std::shared_ptr<lldb_private::OptionValuePathMappings>
    OptionValuePathMappingsSP;
typedef std::shared_ptr<lldb_private::OptionValueProperties>
    OptionValuePropertiesSP;
typedef std::shared_ptr<lldb_private::OptionValueRegex> OptionValueRegexSP;
typedef std::shared_ptr<lldb_private::OptionValueSInt64> OptionValueSInt64SP;
typedef std::shared_ptr<lldb_private::OptionValueString> OptionValueStringSP;
typedef std::shared_ptr<lldb_private::OptionValueUInt64> OptionValueUInt64SP;
typedef std::shared_ptr<lldb_private::OptionValueUUID> OptionValueUUIDSP;
typedef std::shared_ptr<lldb_private::Platform> PlatformSP;
typedef std::shared_ptr<lldb_private::Process> ProcessSP;
typedef std::shared_ptr<lldb_private::ProcessAttachInfo> ProcessAttachInfoSP;
typedef std::shared_ptr<lldb_private::ProcessLaunchInfo> ProcessLaunchInfoSP;
typedef std::weak_ptr<lldb_private::Process> ProcessWP;
typedef std::shared_ptr<lldb_private::Property> PropertySP;
typedef std::shared_ptr<lldb_private::RegisterCheckpoint> RegisterCheckpointSP;
typedef std::shared_ptr<lldb_private::RegisterContext> RegisterContextSP;
typedef std::shared_ptr<lldb_private::RegularExpression> RegularExpressionSP;
typedef std::shared_ptr<lldb_private::Queue> QueueSP;
typedef std::weak_ptr<lldb_private::Queue> QueueWP;
typedef std::shared_ptr<lldb_private::QueueItem> QueueItemSP;
typedef std::shared_ptr<lldb_private::REPL> REPLSP;
typedef std::shared_ptr<lldb_private::RecognizedStackFrame>
    RecognizedStackFrameSP;
typedef std::shared_ptr<lldb_private::ScriptSummaryFormat>
    ScriptSummaryFormatSP;
typedef std::shared_ptr<lldb_private::ScriptInterpreter> ScriptInterpreterSP;
typedef std::unique_ptr<lldb_private::ScriptInterpreter> ScriptInterpreterUP;
typedef std::shared_ptr<lldb_private::Section> SectionSP;
typedef std::unique_ptr<lldb_private::SectionList> SectionListUP;
typedef std::weak_ptr<lldb_private::Section> SectionWP;
typedef std::shared_ptr<lldb_private::SectionLoadList> SectionLoadListSP;
typedef std::shared_ptr<lldb_private::SearchFilter> SearchFilterSP;
typedef std::shared_ptr<lldb_private::Settings> SettingsSP;
typedef std::unique_ptr<lldb_private::SourceManager> SourceManagerUP;
typedef std::shared_ptr<lldb_private::StackFrame> StackFrameSP;
typedef std::unique_ptr<lldb_private::StackFrame> StackFrameUP;
typedef std::weak_ptr<lldb_private::StackFrame> StackFrameWP;
typedef std::shared_ptr<lldb_private::StackFrameList> StackFrameListSP;
typedef std::shared_ptr<lldb_private::StackFrameRecognizer>
    StackFrameRecognizerSP;
typedef std::shared_ptr<lldb_private::StopInfo> StopInfoSP;
typedef std::shared_ptr<lldb_private::StoppointLocation> StoppointLocationSP;
typedef std::shared_ptr<lldb_private::Stream> StreamSP;
typedef std::weak_ptr<lldb_private::Stream> StreamWP;
typedef std::shared_ptr<lldb_private::StreamFile> StreamFileSP;
typedef std::shared_ptr<lldb_private::StringSummaryFormat>
    StringTypeSummaryImplSP;
typedef std::unique_ptr<lldb_private::StructuredDataImpl> StructuredDataImplUP;
typedef std::shared_ptr<lldb_private::StructuredDataPlugin>
    StructuredDataPluginSP;
typedef std::weak_ptr<lldb_private::StructuredDataPlugin>
    StructuredDataPluginWP;
typedef std::shared_ptr<lldb_private::SymbolFile> SymbolFileSP;
typedef std::shared_ptr<lldb_private::SymbolFileType> SymbolFileTypeSP;
typedef std::weak_ptr<lldb_private::SymbolFileType> SymbolFileTypeWP;
typedef std::shared_ptr<lldb_private::SymbolContextSpecifier>
    SymbolContextSpecifierSP;
typedef std::unique_ptr<lldb_private::SymbolVendor> SymbolVendorUP;
typedef std::shared_ptr<lldb_private::SyntheticChildren> SyntheticChildrenSP;
typedef std::shared_ptr<lldb_private::SyntheticChildrenFrontEnd>
    SyntheticChildrenFrontEndSP;
typedef std::shared_ptr<lldb_private::Target> TargetSP;
typedef std::weak_ptr<lldb_private::Target> TargetWP;
typedef std::shared_ptr<lldb_private::TargetProperties> TargetPropertiesSP;
typedef std::shared_ptr<lldb_private::Thread> ThreadSP;
typedef std::weak_ptr<lldb_private::Thread> ThreadWP;
typedef std::shared_ptr<lldb_private::ThreadCollection> ThreadCollectionSP;
typedef std::shared_ptr<lldb_private::ThreadPlan> ThreadPlanSP;
typedef std::shared_ptr<lldb_private::ThreadPlanTracer> ThreadPlanTracerSP;
typedef std::shared_ptr<lldb_private::TraceOptions> TraceOptionsSP;
typedef std::shared_ptr<lldb_private::Type> TypeSP;
typedef std::weak_ptr<lldb_private::Type> TypeWP;
typedef std::shared_ptr<lldb_private::TypeCategoryImpl> TypeCategoryImplSP;
typedef std::shared_ptr<lldb_private::TypeImpl> TypeImplSP;
typedef std::shared_ptr<lldb_private::TypeMemberFunctionImpl>
    TypeMemberFunctionImplSP;
typedef std::shared_ptr<lldb_private::TypeEnumMemberImpl> TypeEnumMemberImplSP;
typedef std::shared_ptr<lldb_private::TypeFilterImpl> TypeFilterImplSP;
typedef std::shared_ptr<lldb_private::TypeSystem> TypeSystemSP;
typedef std::shared_ptr<lldb_private::TypeFormatImpl> TypeFormatImplSP;
typedef std::shared_ptr<lldb_private::TypeNameSpecifierImpl>
    TypeNameSpecifierImplSP;
typedef std::shared_ptr<lldb_private::TypeSummaryImpl> TypeSummaryImplSP;
typedef std::shared_ptr<lldb_private::TypeSummaryOptions> TypeSummaryOptionsSP;
typedef std::shared_ptr<lldb_private::TypeValidatorImpl> TypeValidatorImplSP;
#ifndef LLDB_DISABLE_PYTHON
typedef std::shared_ptr<lldb_private::ScriptedSyntheticChildren>
    ScriptedSyntheticChildrenSP;
#endif
typedef std::shared_ptr<lldb_private::UnixSignals> UnixSignalsSP;
typedef std::weak_ptr<lldb_private::UnixSignals> UnixSignalsWP;
typedef std::shared_ptr<lldb_private::UnwindAssembly> UnwindAssemblySP;
typedef std::shared_ptr<lldb_private::UnwindPlan> UnwindPlanSP;
typedef std::shared_ptr<lldb_private::UtilityFunction> UtilityFunctionSP;
typedef lldb_private::SharingPtr<lldb_private::ValueObject> ValueObjectSP;
typedef std::shared_ptr<lldb_private::Value> ValueSP;
typedef std::shared_ptr<lldb_private::ValueList> ValueListSP;
typedef std::shared_ptr<lldb_private::Variable> VariableSP;
typedef std::shared_ptr<lldb_private::VariableList> VariableListSP;
typedef std::shared_ptr<lldb_private::ValueObjectList> ValueObjectListSP;
typedef std::shared_ptr<lldb_private::Watchpoint> WatchpointSP;

} // namespace lldb

//----------------------------------------------------------------------
// llvm forward declarations
//----------------------------------------------------------------------
namespace llvm {

struct ItaniumPartialDemangler;
class StringRef;

} // namespace llvm

#endif // #if defined(__cplusplus)
#endif // LLDB_lldb_forward_h_
