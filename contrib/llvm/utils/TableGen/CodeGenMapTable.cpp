//===- CodeGenMapTable.cpp - Instruction Mapping Table Generator ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// CodeGenMapTable provides functionality for the TabelGen to create
// relation mapping between instructions. Relation models are defined using
// InstrMapping as a base class. This file implements the functionality which
// parses these definitions and generates relation maps using the information
// specified there. These maps are emitted as tables in the XXXGenInstrInfo.inc
// file along with the functions to query them.
//
// A relationship model to relate non-predicate instructions with their
// predicated true/false forms can be defined as follows:
//
// def getPredOpcode : InstrMapping {
//  let FilterClass = "PredRel";
//  let RowFields = ["BaseOpcode"];
//  let ColFields = ["PredSense"];
//  let KeyCol = ["none"];
//  let ValueCols = [["true"], ["false"]]; }
//
// CodeGenMapTable parses this map and generates a table in XXXGenInstrInfo.inc
// file that contains the instructions modeling this relationship. This table
// is defined in the function
// "int getPredOpcode(uint16_t Opcode, enum PredSense inPredSense)"
// that can be used to retrieve the predicated form of the instruction by
// passing its opcode value and the predicate sense (true/false) of the desired
// instruction as arguments.
//
// Short description of the algorithm:
//
// 1) Iterate through all the records that derive from "InstrMapping" class.
// 2) For each record, filter out instructions based on the FilterClass value.
// 3) Iterate through this set of instructions and insert them into
// RowInstrMap map based on their RowFields values. RowInstrMap is keyed by the
// vector of RowFields values and contains vectors of Records (instructions) as
// values. RowFields is a list of fields that are required to have the same
// values for all the instructions appearing in the same row of the relation
// table. All the instructions in a given row of the relation table have some
// sort of relationship with the key instruction defined by the corresponding
// relationship model.
//
// Ex: RowInstrMap(RowVal1, RowVal2, ...) -> [Instr1, Instr2, Instr3, ... ]
// Here Instr1, Instr2, Instr3 have same values (RowVal1, RowVal2) for
// RowFields. These groups of instructions are later matched against ValueCols
// to determine the column they belong to, if any.
//
// While building the RowInstrMap map, collect all the key instructions in
// KeyInstrVec. These are the instructions having the same values as KeyCol
// for all the fields listed in ColFields.
//
// For Example:
//
// Relate non-predicate instructions with their predicated true/false forms.
//
// def getPredOpcode : InstrMapping {
//  let FilterClass = "PredRel";
//  let RowFields = ["BaseOpcode"];
//  let ColFields = ["PredSense"];
//  let KeyCol = ["none"];
//  let ValueCols = [["true"], ["false"]]; }
//
// Here, only instructions that have "none" as PredSense will be selected as key
// instructions.
//
// 4) For each key instruction, get the group of instructions that share the
// same key-value as the key instruction from RowInstrMap. Iterate over the list
// of columns in ValueCols (it is defined as a list<list<string> >. Therefore,
// it can specify multi-column relationships). For each column, find the
// instruction from the group that matches all the values for the column.
// Multiple matches are not allowed.
//
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"
#include "llvm/Support/Format.h"
#include "llvm/TableGen/Error.h"
using namespace llvm;
typedef std::map<std::string, std::vector<Record*> > InstrRelMapTy;

typedef std::map<std::vector<Init*>, std::vector<Record*> > RowInstrMapTy;

namespace {

//===----------------------------------------------------------------------===//
// This class is used to represent InstrMapping class defined in Target.td file.
class InstrMap {
private:
  std::string Name;
  std::string FilterClass;
  ListInit *RowFields;
  ListInit *ColFields;
  ListInit *KeyCol;
  std::vector<ListInit*> ValueCols;

public:
  InstrMap(Record* MapRec) {
    Name = MapRec->getName();

    // FilterClass - It's used to reduce the search space only to the
    // instructions that define the kind of relationship modeled by
    // this InstrMapping object/record.
    const RecordVal *Filter = MapRec->getValue("FilterClass");
    FilterClass = Filter->getValue()->getAsUnquotedString();

    // List of fields/attributes that need to be same across all the
    // instructions in a row of the relation table.
    RowFields = MapRec->getValueAsListInit("RowFields");

    // List of fields/attributes that are constant across all the instruction
    // in a column of the relation table. Ex: ColFields = 'predSense'
    ColFields = MapRec->getValueAsListInit("ColFields");

    // Values for the fields/attributes listed in 'ColFields'.
    // Ex: KeyCol = 'noPred' -- key instruction is non-predicated
    KeyCol = MapRec->getValueAsListInit("KeyCol");

    // List of values for the fields/attributes listed in 'ColFields', one for
    // each column in the relation table.
    //
    // Ex: ValueCols = [['true'],['false']] -- it results two columns in the
    // table. First column requires all the instructions to have predSense
    // set to 'true' and second column requires it to be 'false'.
    ListInit *ColValList = MapRec->getValueAsListInit("ValueCols");

    // Each instruction map must specify at least one column for it to be valid.
    if (ColValList->empty())
      PrintFatalError(MapRec->getLoc(), "InstrMapping record `" +
        MapRec->getName() + "' has empty " + "`ValueCols' field!");

    for (Init *I : ColValList->getValues()) {
      ListInit *ColI = dyn_cast<ListInit>(I);

      // Make sure that all the sub-lists in 'ValueCols' have same number of
      // elements as the fields in 'ColFields'.
      if (ColI->size() != ColFields->size())
        PrintFatalError(MapRec->getLoc(), "Record `" + MapRec->getName() +
          "', field `ValueCols' entries don't match with " +
          " the entries in 'ColFields'!");
      ValueCols.push_back(ColI);
    }
  }

  std::string getName() const {
    return Name;
  }

  std::string getFilterClass() {
    return FilterClass;
  }

  ListInit *getRowFields() const {
    return RowFields;
  }

  ListInit *getColFields() const {
    return ColFields;
  }

  ListInit *getKeyCol() const {
    return KeyCol;
  }

  const std::vector<ListInit*> &getValueCols() const {
    return ValueCols;
  }
};
} // End anonymous namespace.


//===----------------------------------------------------------------------===//
// class MapTableEmitter : It builds the instruction relation maps using
// the information provided in InstrMapping records. It outputs these
// relationship maps as tables into XXXGenInstrInfo.inc file along with the
// functions to query them.

namespace {
class MapTableEmitter {
private:
//  std::string TargetName;
  const CodeGenTarget &Target;
  // InstrMapDesc - InstrMapping record to be processed.
  InstrMap InstrMapDesc;

  // InstrDefs - list of instructions filtered using FilterClass defined
  // in InstrMapDesc.
  std::vector<Record*> InstrDefs;

  // RowInstrMap - maps RowFields values to the instructions. It's keyed by the
  // values of the row fields and contains vector of records as values.
  RowInstrMapTy RowInstrMap;

  // KeyInstrVec - list of key instructions.
  std::vector<Record*> KeyInstrVec;
  DenseMap<Record*, std::vector<Record*> > MapTable;

public:
  MapTableEmitter(CodeGenTarget &Target, RecordKeeper &Records, Record *IMRec):
                  Target(Target), InstrMapDesc(IMRec) {
    const std::string FilterClass = InstrMapDesc.getFilterClass();
    InstrDefs = Records.getAllDerivedDefinitions(FilterClass);
  }

  void buildRowInstrMap();

  // Returns true if an instruction is a key instruction, i.e., its ColFields
  // have same values as KeyCol.
  bool isKeyColInstr(Record* CurInstr);

  // Find column instruction corresponding to a key instruction based on the
  // constraints for that column.
  Record *getInstrForColumn(Record *KeyInstr, ListInit *CurValueCol);

  // Find column instructions for each key instruction based
  // on ValueCols and store them into MapTable.
  void buildMapTable();

  void emitBinSearch(raw_ostream &OS, unsigned TableSize);
  void emitTablesWithFunc(raw_ostream &OS);
  unsigned emitBinSearchTable(raw_ostream &OS);

  // Lookup functions to query binary search tables.
  void emitMapFuncBody(raw_ostream &OS, unsigned TableSize);

};
} // End anonymous namespace.


//===----------------------------------------------------------------------===//
// Process all the instructions that model this relation (alreday present in
// InstrDefs) and insert them into RowInstrMap which is keyed by the values of
// the fields listed as RowFields. It stores vectors of records as values.
// All the related instructions have the same values for the RowFields thus are
// part of the same key-value pair.
//===----------------------------------------------------------------------===//

void MapTableEmitter::buildRowInstrMap() {
  for (Record *CurInstr : InstrDefs) {
    std::vector<Init*> KeyValue;
    ListInit *RowFields = InstrMapDesc.getRowFields();
    for (Init *RowField : RowFields->getValues()) {
      RecordVal *RecVal = CurInstr->getValue(RowField);
      if (RecVal == nullptr)
        PrintFatalError(CurInstr->getLoc(), "No value " +
                        RowField->getAsString() + " found in \"" +
                        CurInstr->getName() + "\" instruction description.");
      Init *CurInstrVal = RecVal->getValue();
      KeyValue.push_back(CurInstrVal);
    }

    // Collect key instructions into KeyInstrVec. Later, these instructions are
    // processed to assign column position to the instructions sharing
    // their KeyValue in RowInstrMap.
    if (isKeyColInstr(CurInstr))
      KeyInstrVec.push_back(CurInstr);

    RowInstrMap[KeyValue].push_back(CurInstr);
  }
}

//===----------------------------------------------------------------------===//
// Return true if an instruction is a KeyCol instruction.
//===----------------------------------------------------------------------===//

bool MapTableEmitter::isKeyColInstr(Record* CurInstr) {
  ListInit *ColFields = InstrMapDesc.getColFields();
  ListInit *KeyCol = InstrMapDesc.getKeyCol();

  // Check if the instruction is a KeyCol instruction.
  bool MatchFound = true;
  for (unsigned j = 0, endCF = ColFields->size();
      (j < endCF) && MatchFound; j++) {
    RecordVal *ColFieldName = CurInstr->getValue(ColFields->getElement(j));
    std::string CurInstrVal = ColFieldName->getValue()->getAsUnquotedString();
    std::string KeyColValue = KeyCol->getElement(j)->getAsUnquotedString();
    MatchFound = (CurInstrVal == KeyColValue);
  }
  return MatchFound;
}

//===----------------------------------------------------------------------===//
// Build a map to link key instructions with the column instructions arranged
// according to their column positions.
//===----------------------------------------------------------------------===//

void MapTableEmitter::buildMapTable() {
  // Find column instructions for a given key based on the ColField
  // constraints.
  const std::vector<ListInit*> &ValueCols = InstrMapDesc.getValueCols();
  unsigned NumOfCols = ValueCols.size();
  for (Record *CurKeyInstr : KeyInstrVec) {
    std::vector<Record*> ColInstrVec(NumOfCols);

    // Find the column instruction based on the constraints for the column.
    for (unsigned ColIdx = 0; ColIdx < NumOfCols; ColIdx++) {
      ListInit *CurValueCol = ValueCols[ColIdx];
      Record *ColInstr = getInstrForColumn(CurKeyInstr, CurValueCol);
      ColInstrVec[ColIdx] = ColInstr;
    }
    MapTable[CurKeyInstr] = ColInstrVec;
  }
}

//===----------------------------------------------------------------------===//
// Find column instruction based on the constraints for that column.
//===----------------------------------------------------------------------===//

Record *MapTableEmitter::getInstrForColumn(Record *KeyInstr,
                                           ListInit *CurValueCol) {
  ListInit *RowFields = InstrMapDesc.getRowFields();
  std::vector<Init*> KeyValue;

  // Construct KeyValue using KeyInstr's values for RowFields.
  for (Init *RowField : RowFields->getValues()) {
    Init *KeyInstrVal = KeyInstr->getValue(RowField)->getValue();
    KeyValue.push_back(KeyInstrVal);
  }

  // Get all the instructions that share the same KeyValue as the KeyInstr
  // in RowInstrMap. We search through these instructions to find a match
  // for the current column, i.e., the instruction which has the same values
  // as CurValueCol for all the fields in ColFields.
  const std::vector<Record*> &RelatedInstrVec = RowInstrMap[KeyValue];

  ListInit *ColFields = InstrMapDesc.getColFields();
  Record *MatchInstr = nullptr;

  for (unsigned i = 0, e = RelatedInstrVec.size(); i < e; i++) {
    bool MatchFound = true;
    Record *CurInstr = RelatedInstrVec[i];
    for (unsigned j = 0, endCF = ColFields->size();
        (j < endCF) && MatchFound; j++) {
      Init *ColFieldJ = ColFields->getElement(j);
      Init *CurInstrInit = CurInstr->getValue(ColFieldJ)->getValue();
      std::string CurInstrVal = CurInstrInit->getAsUnquotedString();
      Init *ColFieldJVallue = CurValueCol->getElement(j);
      MatchFound = (CurInstrVal == ColFieldJVallue->getAsUnquotedString());
    }

    if (MatchFound) {
      if (MatchInstr) {
        // Already had a match
        // Error if multiple matches are found for a column.
        std::string KeyValueStr;
        for (Init *Value : KeyValue) {
          if (!KeyValueStr.empty())
            KeyValueStr += ", ";
          KeyValueStr += Value->getAsString();
        }

        PrintFatalError("Multiple matches found for `" + KeyInstr->getName() +
              "', for the relation `" + InstrMapDesc.getName() + "', row fields [" +
              KeyValueStr + "], column `" + CurValueCol->getAsString() + "'");
      }
      MatchInstr = CurInstr;
    }
  }
  return MatchInstr;
}

//===----------------------------------------------------------------------===//
// Emit one table per relation. Only instructions with a valid relation of a
// given type are included in the table sorted by their enum values (opcodes).
// Binary search is used for locating instructions in the table.
//===----------------------------------------------------------------------===//

unsigned MapTableEmitter::emitBinSearchTable(raw_ostream &OS) {

  ArrayRef<const CodeGenInstruction*> NumberedInstructions =
                                            Target.getInstructionsByEnumValue();
  StringRef Namespace = Target.getInstNamespace();
  const std::vector<ListInit*> &ValueCols = InstrMapDesc.getValueCols();
  unsigned NumCol = ValueCols.size();
  unsigned TotalNumInstr = NumberedInstructions.size();
  unsigned TableSize = 0;

  OS << "static const uint16_t "<<InstrMapDesc.getName();
  // Number of columns in the table are NumCol+1 because key instructions are
  // emitted as first column.
  OS << "Table[]["<< NumCol+1 << "] = {\n";
  for (unsigned i = 0; i < TotalNumInstr; i++) {
    Record *CurInstr = NumberedInstructions[i]->TheDef;
    std::vector<Record*> ColInstrs = MapTable[CurInstr];
    std::string OutStr("");
    unsigned RelExists = 0;
    if (!ColInstrs.empty()) {
      for (unsigned j = 0; j < NumCol; j++) {
        if (ColInstrs[j] != nullptr) {
          RelExists = 1;
          OutStr += ", ";
          OutStr += Namespace;
          OutStr += "::";
          OutStr += ColInstrs[j]->getName();
        } else { OutStr += ", (uint16_t)-1U";}
      }

      if (RelExists) {
        OS << "  { " << Namespace << "::" << CurInstr->getName();
        OS << OutStr <<" },\n";
        TableSize++;
      }
    }
  }
  if (!TableSize) {
    OS << "  { " << Namespace << "::" << "INSTRUCTION_LIST_END, ";
    OS << Namespace << "::" << "INSTRUCTION_LIST_END }";
  }
  OS << "}; // End of " << InstrMapDesc.getName() << "Table\n\n";
  return TableSize;
}

//===----------------------------------------------------------------------===//
// Emit binary search algorithm as part of the functions used to query
// relation tables.
//===----------------------------------------------------------------------===//

void MapTableEmitter::emitBinSearch(raw_ostream &OS, unsigned TableSize) {
  OS << "  unsigned mid;\n";
  OS << "  unsigned start = 0;\n";
  OS << "  unsigned end = " << TableSize << ";\n";
  OS << "  while (start < end) {\n";
  OS << "    mid = start + (end - start)/2;\n";
  OS << "    if (Opcode == " << InstrMapDesc.getName() << "Table[mid][0]) {\n";
  OS << "      break;\n";
  OS << "    }\n";
  OS << "    if (Opcode < " << InstrMapDesc.getName() << "Table[mid][0])\n";
  OS << "      end = mid;\n";
  OS << "    else\n";
  OS << "      start = mid + 1;\n";
  OS << "  }\n";
  OS << "  if (start == end)\n";
  OS << "    return -1; // Instruction doesn't exist in this table.\n\n";
}

//===----------------------------------------------------------------------===//
// Emit functions to query relation tables.
//===----------------------------------------------------------------------===//

void MapTableEmitter::emitMapFuncBody(raw_ostream &OS,
                                           unsigned TableSize) {

  ListInit *ColFields = InstrMapDesc.getColFields();
  const std::vector<ListInit*> &ValueCols = InstrMapDesc.getValueCols();

  // Emit binary search algorithm to locate instructions in the
  // relation table. If found, return opcode value from the appropriate column
  // of the table.
  emitBinSearch(OS, TableSize);

  if (ValueCols.size() > 1) {
    for (unsigned i = 0, e = ValueCols.size(); i < e; i++) {
      ListInit *ColumnI = ValueCols[i];
      for (unsigned j = 0, ColSize = ColumnI->size(); j < ColSize; ++j) {
        std::string ColName = ColFields->getElement(j)->getAsUnquotedString();
        OS << "  if (in" << ColName;
        OS << " == ";
        OS << ColName << "_" << ColumnI->getElement(j)->getAsUnquotedString();
        if (j < ColumnI->size() - 1) OS << " && ";
        else OS << ")\n";
      }
      OS << "    return " << InstrMapDesc.getName();
      OS << "Table[mid]["<<i+1<<"];\n";
    }
    OS << "  return -1;";
  }
  else
    OS << "  return " << InstrMapDesc.getName() << "Table[mid][1];\n";

  OS <<"}\n\n";
}

//===----------------------------------------------------------------------===//
// Emit relation tables and the functions to query them.
//===----------------------------------------------------------------------===//

void MapTableEmitter::emitTablesWithFunc(raw_ostream &OS) {

  // Emit function name and the input parameters : mostly opcode value of the
  // current instruction. However, if a table has multiple columns (more than 2
  // since first column is used for the key instructions), then we also need
  // to pass another input to indicate the column to be selected.

  ListInit *ColFields = InstrMapDesc.getColFields();
  const std::vector<ListInit*> &ValueCols = InstrMapDesc.getValueCols();
  OS << "// "<< InstrMapDesc.getName() << "\nLLVM_READONLY\n";
  OS << "int "<< InstrMapDesc.getName() << "(uint16_t Opcode";
  if (ValueCols.size() > 1) {
    for (Init *CF : ColFields->getValues()) {
      std::string ColName = CF->getAsUnquotedString();
      OS << ", enum " << ColName << " in" << ColName << ") {\n";
    }
  } else { OS << ") {\n"; }

  // Emit map table.
  unsigned TableSize = emitBinSearchTable(OS);

  // Emit rest of the function body.
  emitMapFuncBody(OS, TableSize);
}

//===----------------------------------------------------------------------===//
// Emit enums for the column fields across all the instruction maps.
//===----------------------------------------------------------------------===//

static void emitEnums(raw_ostream &OS, RecordKeeper &Records) {

  std::vector<Record*> InstrMapVec;
  InstrMapVec = Records.getAllDerivedDefinitions("InstrMapping");
  std::map<std::string, std::vector<Init*> > ColFieldValueMap;

  // Iterate over all InstrMapping records and create a map between column
  // fields and their possible values across all records.
  for (Record *CurMap : InstrMapVec) {
    ListInit *ColFields;
    ColFields = CurMap->getValueAsListInit("ColFields");
    ListInit *List = CurMap->getValueAsListInit("ValueCols");
    std::vector<ListInit*> ValueCols;
    unsigned ListSize = List->size();

    for (unsigned j = 0; j < ListSize; j++) {
      ListInit *ListJ = dyn_cast<ListInit>(List->getElement(j));

      if (ListJ->size() != ColFields->size())
        PrintFatalError("Record `" + CurMap->getName() + "', field "
          "`ValueCols' entries don't match with the entries in 'ColFields' !");
      ValueCols.push_back(ListJ);
    }

    for (unsigned j = 0, endCF = ColFields->size(); j < endCF; j++) {
      for (unsigned k = 0; k < ListSize; k++){
        std::string ColName = ColFields->getElement(j)->getAsUnquotedString();
        ColFieldValueMap[ColName].push_back((ValueCols[k])->getElement(j));
      }
    }
  }

  for (auto &Entry : ColFieldValueMap) {
    std::vector<Init*> FieldValues = Entry.second;

    // Delete duplicate entries from ColFieldValueMap
    for (unsigned i = 0; i < FieldValues.size() - 1; i++) {
      Init *CurVal = FieldValues[i];
      for (unsigned j = i+1; j < FieldValues.size(); j++) {
        if (CurVal == FieldValues[j]) {
          FieldValues.erase(FieldValues.begin()+j);
          --j;
        }
      }
    }

    // Emit enumerated values for the column fields.
    OS << "enum " << Entry.first << " {\n";
    for (unsigned i = 0, endFV = FieldValues.size(); i < endFV; i++) {
      OS << "\t" << Entry.first << "_" << FieldValues[i]->getAsUnquotedString();
      if (i != endFV - 1)
        OS << ",\n";
      else
        OS << "\n};\n\n";
    }
  }
}

namespace llvm {
//===----------------------------------------------------------------------===//
// Parse 'InstrMapping' records and use the information to form relationship
// between instructions. These relations are emitted as a tables along with the
// functions to query them.
//===----------------------------------------------------------------------===//
void EmitMapTable(RecordKeeper &Records, raw_ostream &OS) {
  CodeGenTarget Target(Records);
  StringRef NameSpace = Target.getInstNamespace();
  std::vector<Record*> InstrMapVec;
  InstrMapVec = Records.getAllDerivedDefinitions("InstrMapping");

  if (InstrMapVec.empty())
    return;

  OS << "#ifdef GET_INSTRMAP_INFO\n";
  OS << "#undef GET_INSTRMAP_INFO\n";
  OS << "namespace llvm {\n\n";
  OS << "namespace " << NameSpace << " {\n\n";

  // Emit coulumn field names and their values as enums.
  emitEnums(OS, Records);

  // Iterate over all instruction mapping records and construct relationship
  // maps based on the information specified there.
  //
  for (Record *CurMap : InstrMapVec) {
    MapTableEmitter IMap(Target, Records, CurMap);

    // Build RowInstrMap to group instructions based on their values for
    // RowFields. In the process, also collect key instructions into
    // KeyInstrVec.
    IMap.buildRowInstrMap();

    // Build MapTable to map key instructions with the corresponding column
    // instructions.
    IMap.buildMapTable();

    // Emit map tables and the functions to query them.
    IMap.emitTablesWithFunc(OS);
  }
  OS << "} // End " << NameSpace << " namespace\n";
  OS << "} // End llvm namespace\n";
  OS << "#endif // GET_INSTRMAP_INFO\n\n";
}

} // End llvm namespace
