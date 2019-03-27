#include "llvm/DebugInfo/PDB/DIA/DIAError.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"

using namespace llvm;
using namespace llvm::pdb;

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class DIAErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.pdb.dia"; }
  std::string message(int Condition) const override {
    switch (static_cast<dia_error_code>(Condition)) {
    case dia_error_code::could_not_create_impl:
      return "Failed to connect to DIA at runtime. Verify that Visual Studio "
             "is properly installed, or that msdiaXX.dll is in your PATH.";
    case dia_error_code::invalid_file_format:
      return "Unable to load PDB. The file has an unrecognized format.";
    case dia_error_code::invalid_parameter:
      return "The parameter is incorrect.";
    case dia_error_code::already_loaded:
      return "Unable to load the PDB or EXE, because it is already loaded.";
    case dia_error_code::debug_info_mismatch:
      return "The PDB file and the EXE file do not match.";
    case dia_error_code::unspecified:
      return "An unknown error has occurred.";
    }
    llvm_unreachable("Unrecognized DIAErrorCode");
  }
};

static llvm::ManagedStatic<DIAErrorCategory> DIACategory;
const std::error_category &llvm::pdb::DIAErrCategory() { return *DIACategory; }

char DIAError::ID;
