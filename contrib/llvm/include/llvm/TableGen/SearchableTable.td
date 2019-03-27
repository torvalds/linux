//===- SearchableTable.td ----------------------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the key top-level classes needed to produce a reasonably
// generic table that can be binary-searched. Three types of objects can be
// defined using the classes in this file:
//
// 1. (Generic) Enums. By instantiating the GenericEnum class once, an enum with
// the name of the def is generated. It is guarded by the preprocessor define
// GET_name_DECL, where name is the name of the def.
//
// 2. (Generic) Tables and search indices. By instantiating the GenericTable
// class once, a table with the name of the instantiating def is generated and
// guarded by the GET_name_IMPL preprocessor guard.
//
// Both a primary key and additional secondary keys / search indices can also
// be defined, which result in the generation of lookup functions. Their
// declarations and definitions are all guarded by GET_name_DECL and
// GET_name_IMPL, respectively, where name is the name of the underlying table.
//
// See AArch64SystemOperands.td and its generated header for example uses.
//
//===----------------------------------------------------------------------===//

// Define a record derived from this class to generate a generic enum.
//
// The name of the record is used as the type name of the C++ enum.
class GenericEnum {
  // Name of a TableGen class. The enum will have one entry for each record
  // that derives from that class.
  string FilterClass;

  // (Optional) Name of a field that is present in all collected records and
  // contains the name of enum entries.
  //
  // If NameField is not set, the record names will be used instead.
  string NameField;

  // (Optional) Name of a field that is present in all collected records and
  // contains the numerical value of enum entries.
  //
  // If ValueField is not set, enum values will be assigned automatically,
  // starting at 0, according to a lexicographical sort of the entry names.
  string ValueField;
}

// Define a record derived from this class to generate a generic table. This
// table can have a searchable primary key, and it can also be referenced by
// external search indices.
//
// The name of the record is used as the name of the global primary array of
// entries of the table in C++.
class GenericTable {
  // Name of a class. The table will have one entry for each record that
  // derives from that class.
  string FilterClass;

  // Name of the C++ struct/class type that holds table entries. The
  // declaration of this type is not generated automatically.
  string CppTypeName = FilterClass;

  // List of the names of fields of collected records that contain the data for
  // table entries, in the order that is used for initialization in C++.
  //
  // For each field of the table named XXX, TableGen will look for a value
  // called TypeOf_XXX and use that as a more detailed description of the
  // type of the field if present. This is required for fields whose type
  // cannot be deduced automatically, such as enum fields. For example:
  //
  //   def MyEnum : GenericEnum {
  //     let FilterClass = "MyEnum";
  //     ...
  //   }
  //
  //   class MyTableEntry {
  //     MyEnum V;
  //     ...
  //   }
  //
  //   def MyTable : GenericTable {
  //     let FilterClass = "MyTableEntry";
  //     let Fields = ["V", ...];
  //     GenericEnum TypeOf_V = MyEnum;
  //   }
  //
  // Fields of type bit, bits<N>, string, Intrinsic, and Instruction (or
  // derived classes of those) are supported natively.
  //
  // Additionally, fields of type `code` can appear, where the value is used
  // verbatim as an initializer. However, these fields cannot be used as
  // search keys.
  list<string> Fields;

  // (Optional) List of fields that make up the primary key.
  list<string> PrimaryKey;

  // (Optional) Name of the primary key search function.
  string PrimaryKeyName;

  // See SearchIndex.EarlyOut
  bit PrimaryKeyEarlyOut = 0;
}

// Define a record derived from this class to generate an additional search
// index for a generic table that has been defined earlier.
//
// The name of the record will be used as the name of the C++ lookup function.
class SearchIndex {
  // Table that this search index refers to.
  GenericTable Table;

  // List of fields that make up the key.
  list<string> Key;

  // If true, the lookup function will check the first field of the key against
  // the minimum and maximum values in the index before entering the binary
  // search. This is convenient for tables that add extended data for a subset
  // of a larger enum-based space, e.g. extended data about a subset of
  // instructions.
  //
  // Can only be used when the first field is an integral (non-string) type.
  bit EarlyOut = 0;
}

// Legacy table type with integrated enum.
class SearchableTable {
  list<string> SearchableFields;
  string EnumNameField = "Name";
  string EnumValueField;
}
