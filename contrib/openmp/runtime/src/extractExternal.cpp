/*
 * extractExternal.cpp
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <string>
#include <strstream>

/* Given a set of n object files h ('external' object files) and a set of m
   object files o ('internal' object files),
   1. Determines r, the subset of h that o depends on, directly or indirectly
   2. Removes the files in h - r from the file system
   3. For each external symbol defined in some file in r, rename it in r U o
      by prefixing it with "__kmp_external_"
   Usage:
   hide.exe <n> <filenames for h> <filenames for o>

   Thus, the prefixed symbols become hidden in the sense that they now have a
   special prefix.
*/

using namespace std;

void stop(char *errorMsg) {
  printf("%s\n", errorMsg);
  exit(1);
}

// an entry in the symbol table of a .OBJ file
class Symbol {
public:
  __int64 name;
  unsigned value;
  unsigned short sectionNum, type;
  char storageClass, nAux;
};

class _rstream : public istrstream {
private:
  const char *buf;

protected:
  _rstream(pair<const char *, streamsize> p)
      : istrstream(p.first, p.second), buf(p.first) {}
  ~_rstream() { delete[] buf; }
};

// A stream encapuslating the content of a file or the content of a string,
// overriding the >> operator to read various integer types in binary form,
// as well as a symbol table entry.
class rstream : public _rstream {
private:
  template <class T> inline rstream &doRead(T &x) {
    read((char *)&x, sizeof(T));
    return *this;
  }
  static pair<const char *, streamsize> getBuf(const char *fileName) {
    ifstream raw(fileName, ios::binary | ios::in);
    if (!raw.is_open())
      stop("rstream.getBuf: Error opening file");
    raw.seekg(0, ios::end);
    streampos fileSize = raw.tellg();
    if (fileSize < 0)
      stop("rstream.getBuf: Error reading file");
    char *buf = new char[fileSize];
    raw.seekg(0, ios::beg);
    raw.read(buf, fileSize);
    return pair<const char *, streamsize>(buf, fileSize);
  }

public:
  // construct from a string
  rstream(const char *buf, streamsize size)
      : _rstream(pair<const char *, streamsize>(buf, size)) {}
  // construct from a file whole content is fully read once to initialize the
  // content of this stream
  rstream(const char *fileName) : _rstream(getBuf(fileName)) {}
  rstream &operator>>(int &x) { return doRead(x); }
  rstream &operator>>(unsigned &x) { return doRead(x); }
  rstream &operator>>(short &x) { return doRead(x); }
  rstream &operator>>(unsigned short &x) { return doRead(x); }
  rstream &operator>>(Symbol &e) {
    read((char *)&e, 18);
    return *this;
  }
};

// string table in a .OBJ file
class StringTable {
private:
  map<string, unsigned> directory;
  size_t length;
  char *data;

  // make <directory> from <length> bytes in <data>
  void makeDirectory(void) {
    unsigned i = 4;
    while (i < length) {
      string s = string(data + i);
      directory.insert(make_pair(s, i));
      i += s.size() + 1;
    }
  }
  // initialize <length> and <data> with contents specified by the arguments
  void init(const char *_data) {
    unsigned _length = *(unsigned *)_data;

    if (_length < sizeof(unsigned) || _length != *(unsigned *)_data)
      stop("StringTable.init: Invalid symbol table");
    if (_data[_length - 1]) {
      // to prevent runaway strings, make sure the data ends with a zero
      data = new char[length = _length + 1];
      data[_length] = 0;
    } else {
      data = new char[length = _length];
    }
    *(unsigned *)data = length;
    KMP_MEMCPY(data + sizeof(unsigned), _data + sizeof(unsigned),
               length - sizeof(unsigned));
    makeDirectory();
  }

public:
  StringTable(rstream &f) {
    // Construct string table by reading from f.
    streampos s;
    unsigned strSize;
    char *strData;

    s = f.tellg();
    f >> strSize;
    if (strSize < sizeof(unsigned))
      stop("StringTable: Invalid string table");
    strData = new char[strSize];
    *(unsigned *)strData = strSize;
    // read the raw data into <strData>
    f.read(strData + sizeof(unsigned), strSize - sizeof(unsigned));
    s = f.tellg() - s;
    if (s < strSize)
      stop("StringTable: Unexpected EOF");
    init(strData);
    delete[] strData;
  }
  StringTable(const set<string> &strings) {
    // Construct string table from given strings.
    char *p;
    set<string>::const_iterator it;
    size_t s;

    // count required size for data
    for (length = sizeof(unsigned), it = strings.begin(); it != strings.end();
         ++it) {
      size_t l = (*it).size();

      if (l > (unsigned)0xFFFFFFFF)
        stop("StringTable: String too long");
      if (l > 8) {
        length += l + 1;
        if (length > (unsigned)0xFFFFFFFF)
          stop("StringTable: Symbol table too long");
      }
    }
    data = new char[length];
    *(unsigned *)data = length;
    // populate data and directory
    for (p = data + sizeof(unsigned), it = strings.begin(); it != strings.end();
         ++it) {
      const string &str = *it;
      size_t l = str.size();
      if (l > 8) {
        directory.insert(make_pair(str, p - data));
        KMP_MEMCPY(p, str.c_str(), l);
        p[l] = 0;
        p += l + 1;
      }
    }
  }
  ~StringTable() { delete[] data; }
  // Returns encoding for given string based on this string table. Error if
  // string length is greater than 8 but string is not in the string table
  // -- returns 0.
  __int64 encode(const string &str) {
    __int64 r;

    if (str.size() <= 8) {
      // encoded directly
      ((char *)&r)[7] = 0;
      KMP_STRNCPY_S((char *)&r, sizeof(r), str.c_str(), 8);
      return r;
    } else {
      // represented as index into table
      map<string, unsigned>::const_iterator it = directory.find(str);
      if (it == directory.end())
        stop("StringTable::encode: String now found in string table");
      ((unsigned *)&r)[0] = 0;
      ((unsigned *)&r)[1] = (*it).second;
      return r;
    }
  }
  // Returns string represented by x based on this string table. Error if x
  // references an invalid position in the table--returns the empty string.
  string decode(__int64 x) const {
    if (*(unsigned *)&x == 0) {
      // represented as index into table
      unsigned &p = ((unsigned *)&x)[1];
      if (p >= length)
        stop("StringTable::decode: Invalid string table lookup");
      return string(data + p);
    } else {
      // encoded directly
      char *p = (char *)&x;
      int i;

      for (i = 0; i < 8 && p[i]; ++i)
        ;
      return string(p, i);
    }
  }
  void write(ostream &os) { os.write(data, length); }
};

// for the named object file, determines the set of defined symbols and the set
// of undefined external symbols and writes them to <defined> and <undefined>
// respectively
void computeExternalSymbols(const char *fileName, set<string> *defined,
                            set<string> *undefined) {
  streampos fileSize;
  size_t strTabStart;
  unsigned symTabStart, symNEntries;
  rstream f(fileName);

  f.seekg(0, ios::end);
  fileSize = f.tellg();

  f.seekg(8);
  f >> symTabStart >> symNEntries;
  // seek to the string table
  f.seekg(strTabStart = symTabStart + 18 * (size_t)symNEntries);
  if (f.eof()) {
    printf("computeExternalSymbols: fileName='%s', fileSize = %lu, symTabStart "
           "= %u, symNEntries = %u\n",
           fileName, (unsigned long)fileSize, symTabStart, symNEntries);
    stop("computeExternalSymbols: Unexpected EOF 1");
  }
  StringTable stringTable(f); // read the string table
  if (f.tellg() != fileSize)
    stop("computeExternalSymbols: Unexpected data after string table");

  f.clear();
  f.seekg(symTabStart); // seek to the symbol table

  defined->clear();
  undefined->clear();
  for (int i = 0; i < symNEntries; ++i) {
    // process each entry
    Symbol e;

    if (f.eof())
      stop("computeExternalSymbols: Unexpected EOF 2");
    f >> e;
    if (f.fail())
      stop("computeExternalSymbols: File read error");
    if (e.nAux) { // auxiliary entry: skip
      f.seekg(e.nAux * 18, ios::cur);
      i += e.nAux;
    }
    // if symbol is extern and defined in the current file, insert it
    if (e.storageClass == 2)
      if (e.sectionNum)
        defined->insert(stringTable.decode(e.name));
      else
        undefined->insert(stringTable.decode(e.name));
  }
}

// For each occurrence of an external symbol in the object file named by
// by <fileName> that is a member of <hide>, renames it by prefixing
// with "__kmp_external_", writing back the file in-place
void hideSymbols(char *fileName, const set<string> &hide) {
  static const string prefix("__kmp_external_");
  set<string> strings; // set of all occurring symbols, appropriately prefixed
  streampos fileSize;
  size_t strTabStart;
  unsigned symTabStart, symNEntries;
  int i;
  rstream in(fileName);

  in.seekg(0, ios::end);
  fileSize = in.tellg();

  in.seekg(8);
  in >> symTabStart >> symNEntries;
  in.seekg(strTabStart = symTabStart + 18 * (size_t)symNEntries);
  if (in.eof())
    stop("hideSymbols: Unexpected EOF");
  StringTable stringTableOld(in); // read original string table

  if (in.tellg() != fileSize)
    stop("hideSymbols: Unexpected data after string table");

  // compute set of occurring strings with prefix added
  for (i = 0; i < symNEntries; ++i) {
    Symbol e;

    in.seekg(symTabStart + i * 18);
    if (in.eof())
      stop("hideSymbols: Unexpected EOF");
    in >> e;
    if (in.fail())
      stop("hideSymbols: File read error");
    if (e.nAux)
      i += e.nAux;
    const string &s = stringTableOld.decode(e.name);
    // if symbol is extern and found in <hide>, prefix and insert into strings,
    // otherwise, just insert into strings without prefix
    strings.insert(
        (e.storageClass == 2 && hide.find(s) != hide.end()) ? prefix + s : s);
  }

  ofstream out(fileName, ios::trunc | ios::out | ios::binary);
  if (!out.is_open())
    stop("hideSymbols: Error opening output file");

  // make new string table from string set
  StringTable stringTableNew = StringTable(strings);

  // copy input file to output file up to just before the symbol table
  in.seekg(0);
  char *buf = new char[symTabStart];
  in.read(buf, symTabStart);
  out.write(buf, symTabStart);
  delete[] buf;

  // copy input symbol table to output symbol table with name translation
  for (i = 0; i < symNEntries; ++i) {
    Symbol e;

    in.seekg(symTabStart + i * 18);
    if (in.eof())
      stop("hideSymbols: Unexpected EOF");
    in >> e;
    if (in.fail())
      stop("hideSymbols: File read error");
    const string &s = stringTableOld.decode(e.name);
    out.seekp(symTabStart + i * 18);
    e.name = stringTableNew.encode(
        (e.storageClass == 2 && hide.find(s) != hide.end()) ? prefix + s : s);
    out.write((char *)&e, 18);
    if (out.fail())
      stop("hideSymbols: File write error");
    if (e.nAux) {
      // copy auxiliary symbol table entries
      int nAux = e.nAux;
      for (int j = 1; j <= nAux; ++j) {
        in >> e;
        out.seekp(symTabStart + (i + j) * 18);
        out.write((char *)&e, 18);
      }
      i += nAux;
    }
  }
  // output string table
  stringTableNew.write(out);
}

// returns true iff <a> and <b> have no common element
template <class T> bool isDisjoint(const set<T> &a, const set<T> &b) {
  set<T>::const_iterator ita, itb;

  for (ita = a.begin(), itb = b.begin(); ita != a.end() && itb != b.end();) {
    const T &ta = *ita, &tb = *itb;
    if (ta < tb)
      ++ita;
    else if (tb < ta)
      ++itb;
    else
      return false;
  }
  return true;
}

// PRE: <defined> and <undefined> are arrays with <nTotal> elements where
// <nTotal> >= <nExternal>.  The first <nExternal> elements correspond to the
// external object files and the rest correspond to the internal object files.
// POST: file x is said to depend on file y if undefined[x] and defined[y] are
// not disjoint. Returns the transitive closure of the set of internal object
// files, as a set of file indexes, under the 'depends on' relation, minus the
// set of internal object files.
set<int> *findRequiredExternal(int nExternal, int nTotal, set<string> *defined,
                               set<string> *undefined) {
  set<int> *required = new set<int>;
  set<int> fresh[2];
  int i, cur = 0;
  bool changed;

  for (i = nTotal - 1; i >= nExternal; --i)
    fresh[cur].insert(i);
  do {
    changed = false;
    for (set<int>::iterator it = fresh[cur].begin(); it != fresh[cur].end();
         ++it) {
      set<string> &s = undefined[*it];

      for (i = 0; i < nExternal; ++i) {
        if (required->find(i) == required->end()) {
          if (!isDisjoint(defined[i], s)) {
            // found a new qualifying element
            required->insert(i);
            fresh[1 - cur].insert(i);
            changed = true;
          }
        }
      }
    }
    fresh[cur].clear();
    cur = 1 - cur;
  } while (changed);
  return required;
}

int main(int argc, char **argv) {
  int nExternal, nInternal, i;
  set<string> *defined, *undefined;
  set<int>::iterator it;

  if (argc < 3)
    stop("Please specify a positive integer followed by a list of object "
         "filenames");
  nExternal = atoi(argv[1]);
  if (nExternal <= 0)
    stop("Please specify a positive integer followed by a list of object "
         "filenames");
  if (nExternal + 2 > argc)
    stop("Too few external objects");
  nInternal = argc - nExternal - 2;
  defined = new set<string>[argc - 2];
  undefined = new set<string>[argc - 2];

  // determine the set of defined and undefined external symbols
  for (i = 2; i < argc; ++i)
    computeExternalSymbols(argv[i], defined + i - 2, undefined + i - 2);

  // determine the set of required external files
  set<int> *requiredExternal =
      findRequiredExternal(nExternal, argc - 2, defined, undefined);
  set<string> hide;

  // determine the set of symbols to hide--namely defined external symbols of
  // the required external files
  for (it = requiredExternal->begin(); it != requiredExternal->end(); ++it) {
    int idx = *it;
    set<string>::iterator it2;
    // We have to insert one element at a time instead of inserting a range
    // because the insert member function taking a range doesn't exist on
    // Windows* OS, at least at the time of this writing.
    for (it2 = defined[idx].begin(); it2 != defined[idx].end(); ++it2)
      hide.insert(*it2);
  }

  // process the external files--removing those that are not required and hiding
  //   the appropriate symbols in the others
  for (i = 0; i < nExternal; ++i)
    if (requiredExternal->find(i) != requiredExternal->end())
      hideSymbols(argv[2 + i], hide);
    else
      remove(argv[2 + i]);
  // hide the appropriate symbols in the internal files
  for (i = nExternal + 2; i < argc; ++i)
    hideSymbols(argv[i], hide);
  return 0;
}
