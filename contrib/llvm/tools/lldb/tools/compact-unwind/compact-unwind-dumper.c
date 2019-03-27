#include <fcntl.h>
#include <inttypes.h>
#include <mach-o/compact_unwind_encoding.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/machine.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

enum {
  UNWIND_ARM64_MODE_MASK = 0x0F000000,
  UNWIND_ARM64_MODE_FRAMELESS = 0x02000000,
  UNWIND_ARM64_MODE_DWARF = 0x03000000,
  UNWIND_ARM64_MODE_FRAME = 0x04000000,

  UNWIND_ARM64_FRAME_X19_X20_PAIR = 0x00000001,
  UNWIND_ARM64_FRAME_X21_X22_PAIR = 0x00000002,
  UNWIND_ARM64_FRAME_X23_X24_PAIR = 0x00000004,
  UNWIND_ARM64_FRAME_X25_X26_PAIR = 0x00000008,
  UNWIND_ARM64_FRAME_X27_X28_PAIR = 0x00000010,
  UNWIND_ARM64_FRAME_D8_D9_PAIR = 0x00000100,
  UNWIND_ARM64_FRAME_D10_D11_PAIR = 0x00000200,
  UNWIND_ARM64_FRAME_D12_D13_PAIR = 0x00000400,
  UNWIND_ARM64_FRAME_D14_D15_PAIR = 0x00000800,

  UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK = 0x00FFF000,
  UNWIND_ARM64_DWARF_SECTION_OFFSET = 0x00FFFFFF,
};

enum {
  UNWIND_ARM_MODE_MASK = 0x0F000000,
  UNWIND_ARM_MODE_FRAME = 0x01000000,
  UNWIND_ARM_MODE_FRAME_D = 0x02000000,
  UNWIND_ARM_MODE_DWARF = 0x04000000,

  UNWIND_ARM_FRAME_STACK_ADJUST_MASK = 0x00C00000,

  UNWIND_ARM_FRAME_FIRST_PUSH_R4 = 0x00000001,
  UNWIND_ARM_FRAME_FIRST_PUSH_R5 = 0x00000002,
  UNWIND_ARM_FRAME_FIRST_PUSH_R6 = 0x00000004,

  UNWIND_ARM_FRAME_SECOND_PUSH_R8 = 0x00000008,
  UNWIND_ARM_FRAME_SECOND_PUSH_R9 = 0x00000010,
  UNWIND_ARM_FRAME_SECOND_PUSH_R10 = 0x00000020,
  UNWIND_ARM_FRAME_SECOND_PUSH_R11 = 0x00000040,
  UNWIND_ARM_FRAME_SECOND_PUSH_R12 = 0x00000080,

  UNWIND_ARM_FRAME_D_REG_COUNT_MASK = 0x00000700,

  UNWIND_ARM_DWARF_SECTION_OFFSET = 0x00FFFFFF,
};

#define EXTRACT_BITS(value, mask)                                              \
  ((value >> __builtin_ctz(mask)) & (((1 << __builtin_popcount(mask))) - 1))

// A quick sketch of a program which can parse the compact unwind info
// used on Darwin systems for exception handling.  The output of
// unwinddump will be more authoritative/reliable but this program
// can dump at least the UNWIND_X86_64_MODE_RBP_FRAME format entries
// correctly.

struct symbol {
  uint64_t file_address;
  const char *name;
};

int symbol_compare(const void *a, const void *b) {
  return (int)((struct symbol *)a)->file_address -
         ((struct symbol *)b)->file_address;
}

struct baton {
  cpu_type_t cputype;

  uint8_t *mach_header_start;    // pointer into this program's address space
  uint8_t *compact_unwind_start; // pointer into this program's address space

  int addr_size; // 4 or 8 bytes, the size of addresses in this file

  uint64_t text_segment_vmaddr; // __TEXT segment vmaddr
  uint64_t text_segment_file_offset;

  uint64_t text_section_vmaddr; // __TEXT,__text section vmaddr
  uint64_t text_section_file_offset;

  uint64_t eh_section_file_address; // the file address of the __TEXT,__eh_frame
                                    // section

  uint8_t
      *lsda_array_start; // for the currently-being-processed first-level index
  uint8_t
      *lsda_array_end; // the lsda_array_start for the NEXT first-level index

  struct symbol *symbols;
  int symbols_count;

  uint64_t *function_start_addresses;
  int function_start_addresses_count;

  int current_index_table_number;

  struct unwind_info_section_header unwind_header;
  struct unwind_info_section_header_index_entry first_level_index_entry;
  struct unwind_info_compressed_second_level_page_header
      compressed_second_level_page_header;
  struct unwind_info_regular_second_level_page_header
      regular_second_level_page_header;
};

uint64_t read_leb128(uint8_t **offset) {
  uint64_t result = 0;
  int shift = 0;
  while (1) {
    uint8_t byte = **offset;
    *offset = *offset + 1;
    result |= (byte & 0x7f) << shift;
    if ((byte & 0x80) == 0)
      break;
    shift += 7;
  }

  return result;
}

// step through the load commands in a thin mach-o binary,
// find the cputype and the start of the __TEXT,__unwind_info
// section, return a pointer to that section or NULL if not found.

static void scan_macho_load_commands(struct baton *baton) {
  struct symtab_command symtab_cmd;
  uint64_t linkedit_segment_vmaddr;
  uint64_t linkedit_segment_file_offset;

  baton->compact_unwind_start = 0;

  uint32_t *magic = (uint32_t *)baton->mach_header_start;

  if (*magic != MH_MAGIC && *magic != MH_MAGIC_64) {
    printf("Unexpected magic number 0x%x in header, exiting.", *magic);
    exit(1);
  }

  bool is_64bit = false;
  if (*magic == MH_MAGIC_64)
    is_64bit = true;

  uint8_t *offset = baton->mach_header_start;

  struct mach_header mh;
  memcpy(&mh, offset, sizeof(struct mach_header));
  if (is_64bit)
    offset += sizeof(struct mach_header_64);
  else
    offset += sizeof(struct mach_header);

  if (is_64bit)
    baton->addr_size = 8;
  else
    baton->addr_size = 4;

  baton->cputype = mh.cputype;

  uint8_t *start_of_load_commands = offset;

  uint32_t cur_cmd = 0;
  while (cur_cmd < mh.ncmds &&
         (offset - start_of_load_commands) < mh.sizeofcmds) {
    struct load_command lc;
    uint32_t *lc_cmd = (uint32_t *)offset;
    uint32_t *lc_cmdsize = (uint32_t *)offset + 1;
    uint8_t *start_of_this_load_cmd = offset;

    if (*lc_cmd == LC_SEGMENT || *lc_cmd == LC_SEGMENT_64) {
      char segment_name[17];
      segment_name[0] = '\0';
      uint32_t nsects = 0;
      uint64_t segment_offset = 0;
      uint64_t segment_vmaddr = 0;

      if (*lc_cmd == LC_SEGMENT_64) {
        struct segment_command_64 seg;
        memcpy(&seg, offset, sizeof(struct segment_command_64));
        memcpy(&segment_name, &seg.segname, 16);
        segment_name[16] = '\0';
        nsects = seg.nsects;
        segment_offset = seg.fileoff;
        segment_vmaddr = seg.vmaddr;
        offset += sizeof(struct segment_command_64);
        if ((seg.flags & SG_PROTECTED_VERSION_1) == SG_PROTECTED_VERSION_1) {
          printf("Segment '%s' is encrypted.\n", segment_name);
        }
      }

      if (*lc_cmd == LC_SEGMENT) {
        struct segment_command seg;
        memcpy(&seg, offset, sizeof(struct segment_command));
        memcpy(&segment_name, &seg.segname, 16);
        segment_name[16] = '\0';
        nsects = seg.nsects;
        segment_offset = seg.fileoff;
        segment_vmaddr = seg.vmaddr;
        offset += sizeof(struct segment_command);
        if ((seg.flags & SG_PROTECTED_VERSION_1) == SG_PROTECTED_VERSION_1) {
          printf("Segment '%s' is encrypted.\n", segment_name);
        }
      }

      if (nsects != 0 && strcmp(segment_name, "__TEXT") == 0) {
        baton->text_segment_vmaddr = segment_vmaddr;
        baton->text_segment_file_offset = segment_offset;

        uint32_t current_sect = 0;
        while (current_sect < nsects &&
               (offset - start_of_this_load_cmd) < *lc_cmdsize) {
          char sect_name[17];
          memcpy(&sect_name, offset, 16);
          sect_name[16] = '\0';
          if (strcmp(sect_name, "__unwind_info") == 0) {
            if (is_64bit) {
              struct section_64 sect;
              memset(&sect, 0, sizeof(struct section_64));
              memcpy(&sect, offset, sizeof(struct section_64));
              baton->compact_unwind_start =
                  baton->mach_header_start + sect.offset;
            } else {
              struct section sect;
              memset(&sect, 0, sizeof(struct section));
              memcpy(&sect, offset, sizeof(struct section));
              baton->compact_unwind_start =
                  baton->mach_header_start + sect.offset;
            }
          }
          if (strcmp(sect_name, "__eh_frame") == 0) {
            if (is_64bit) {
              struct section_64 sect;
              memset(&sect, 0, sizeof(struct section_64));
              memcpy(&sect, offset, sizeof(struct section_64));
              baton->eh_section_file_address = sect.addr;
            } else {
              struct section sect;
              memset(&sect, 0, sizeof(struct section));
              memcpy(&sect, offset, sizeof(struct section));
              baton->eh_section_file_address = sect.addr;
            }
          }
          if (strcmp(sect_name, "__text") == 0) {
            if (is_64bit) {
              struct section_64 sect;
              memset(&sect, 0, sizeof(struct section_64));
              memcpy(&sect, offset, sizeof(struct section_64));
              baton->text_section_vmaddr = sect.addr;
              baton->text_section_file_offset = sect.offset;
            } else {
              struct section sect;
              memset(&sect, 0, sizeof(struct section));
              memcpy(&sect, offset, sizeof(struct section));
              baton->text_section_vmaddr = sect.addr;
            }
          }
          if (is_64bit) {
            offset += sizeof(struct section_64);
          } else {
            offset += sizeof(struct section);
          }
        }
      }

      if (strcmp(segment_name, "__LINKEDIT") == 0) {
        linkedit_segment_vmaddr = segment_vmaddr;
        linkedit_segment_file_offset = segment_offset;
      }
    }

    if (*lc_cmd == LC_SYMTAB) {
      memcpy(&symtab_cmd, offset, sizeof(struct symtab_command));
    }

    if (*lc_cmd == LC_DYSYMTAB) {
      struct dysymtab_command dysymtab_cmd;
      memcpy(&dysymtab_cmd, offset, sizeof(struct dysymtab_command));

      int nlist_size = 12;
      if (is_64bit)
        nlist_size = 16;

      char *string_table =
          (char *)(baton->mach_header_start + symtab_cmd.stroff);
      uint8_t *local_syms = baton->mach_header_start + symtab_cmd.symoff +
                            (dysymtab_cmd.ilocalsym * nlist_size);
      int local_syms_count = dysymtab_cmd.nlocalsym;
      uint8_t *exported_syms = baton->mach_header_start + symtab_cmd.symoff +
                               (dysymtab_cmd.iextdefsym * nlist_size);
      int exported_syms_count = dysymtab_cmd.nextdefsym;

      // We're only going to create records for a small number of these symbols
      // but to
      // simplify the memory management I'll allocate enough space to store all
      // of them.
      baton->symbols = (struct symbol *)malloc(
          sizeof(struct symbol) * (local_syms_count + exported_syms_count));
      baton->symbols_count = 0;

      for (int i = 0; i < local_syms_count; i++) {
        struct nlist_64 nlist;
        memset(&nlist, 0, sizeof(struct nlist_64));
        if (is_64bit) {
          memcpy(&nlist, local_syms + (i * nlist_size),
                 sizeof(struct nlist_64));
        } else {
          struct nlist nlist_32;
          memset(&nlist_32, 0, sizeof(struct nlist));
          memcpy(&nlist_32, local_syms + (i * nlist_size),
                 sizeof(struct nlist));
          nlist.n_un.n_strx = nlist_32.n_un.n_strx;
          nlist.n_type = nlist_32.n_type;
          nlist.n_sect = nlist_32.n_sect;
          nlist.n_desc = nlist_32.n_desc;
          nlist.n_value = nlist_32.n_value;
        }
        if ((nlist.n_type & N_STAB) == 0 &&
            ((nlist.n_type & N_EXT) == 1 ||
             ((nlist.n_type & N_TYPE) == N_TYPE && nlist.n_sect != NO_SECT)) &&
            nlist.n_value != 0 && nlist.n_value != baton->text_segment_vmaddr) {
          baton->symbols[baton->symbols_count].file_address = nlist.n_value;
          if (baton->cputype == CPU_TYPE_ARM)
            baton->symbols[baton->symbols_count].file_address =
                baton->symbols[baton->symbols_count].file_address & ~1;
          baton->symbols[baton->symbols_count].name =
              string_table + nlist.n_un.n_strx;
          baton->symbols_count++;
        }
      }

      for (int i = 0; i < exported_syms_count; i++) {
        struct nlist_64 nlist;
        memset(&nlist, 0, sizeof(struct nlist_64));
        if (is_64bit) {
          memcpy(&nlist, exported_syms + (i * nlist_size),
                 sizeof(struct nlist_64));
        } else {
          struct nlist nlist_32;
          memcpy(&nlist_32, exported_syms + (i * nlist_size),
                 sizeof(struct nlist));
          nlist.n_un.n_strx = nlist_32.n_un.n_strx;
          nlist.n_type = nlist_32.n_type;
          nlist.n_sect = nlist_32.n_sect;
          nlist.n_desc = nlist_32.n_desc;
          nlist.n_value = nlist_32.n_value;
        }
        if ((nlist.n_type & N_STAB) == 0 &&
            ((nlist.n_type & N_EXT) == 1 ||
             ((nlist.n_type & N_TYPE) == N_TYPE && nlist.n_sect != NO_SECT)) &&
            nlist.n_value != 0 && nlist.n_value != baton->text_segment_vmaddr) {
          baton->symbols[baton->symbols_count].file_address = nlist.n_value;
          if (baton->cputype == CPU_TYPE_ARM)
            baton->symbols[baton->symbols_count].file_address =
                baton->symbols[baton->symbols_count].file_address & ~1;
          baton->symbols[baton->symbols_count].name =
              string_table + nlist.n_un.n_strx;
          baton->symbols_count++;
        }
      }

      qsort(baton->symbols, baton->symbols_count, sizeof(struct symbol),
            symbol_compare);
    }

    if (*lc_cmd == LC_FUNCTION_STARTS) {
      struct linkedit_data_command function_starts_cmd;
      memcpy(&function_starts_cmd, offset,
             sizeof(struct linkedit_data_command));

      uint8_t *funcstarts_offset =
          baton->mach_header_start + function_starts_cmd.dataoff;
      uint8_t *function_end = funcstarts_offset + function_starts_cmd.datasize;
      int count = 0;

      while (funcstarts_offset < function_end) {
        if (read_leb128(&funcstarts_offset) != 0) {
          count++;
        }
      }

      baton->function_start_addresses =
          (uint64_t *)malloc(sizeof(uint64_t) * count);
      baton->function_start_addresses_count = count;

      funcstarts_offset =
          baton->mach_header_start + function_starts_cmd.dataoff;
      uint64_t current_pc = baton->text_segment_vmaddr;
      int i = 0;
      while (funcstarts_offset < function_end) {
        uint64_t func_start = read_leb128(&funcstarts_offset);
        if (func_start != 0) {
          current_pc += func_start;
          baton->function_start_addresses[i++] = current_pc;
        }
      }
    }

    offset = start_of_this_load_cmd + *lc_cmdsize;
    cur_cmd++;
  }

  // Augment the symbol table with the function starts table -- adding symbol
  // entries
  // for functions that were stripped.

  int unnamed_functions_to_add = 0;
  for (int i = 0; i < baton->function_start_addresses_count; i++) {
    struct symbol search_key;
    search_key.file_address = baton->function_start_addresses[i];
    if (baton->cputype == CPU_TYPE_ARM)
      search_key.file_address = search_key.file_address & ~1;
    struct symbol *sym =
        bsearch(&search_key, baton->symbols, baton->symbols_count,
                sizeof(struct symbol), symbol_compare);
    if (sym == NULL)
      unnamed_functions_to_add++;
  }

  baton->symbols = (struct symbol *)realloc(
      baton->symbols, sizeof(struct symbol) *
                          (baton->symbols_count + unnamed_functions_to_add));

  int current_unnamed_symbol = 1;
  int number_symbols_added = 0;
  for (int i = 0; i < baton->function_start_addresses_count; i++) {
    struct symbol search_key;
    search_key.file_address = baton->function_start_addresses[i];
    if (baton->cputype == CPU_TYPE_ARM)
      search_key.file_address = search_key.file_address & ~1;
    struct symbol *sym =
        bsearch(&search_key, baton->symbols, baton->symbols_count,
                sizeof(struct symbol), symbol_compare);
    if (sym == NULL) {
      char *name;
      asprintf(&name, "unnamed function #%d", current_unnamed_symbol++);
      baton->symbols[baton->symbols_count + number_symbols_added].file_address =
          baton->function_start_addresses[i];
      baton->symbols[baton->symbols_count + number_symbols_added].name = name;
      number_symbols_added++;
    }
  }
  baton->symbols_count += number_symbols_added;
  qsort(baton->symbols, baton->symbols_count, sizeof(struct symbol),
        symbol_compare);

  //    printf ("function start addresses\n");
  //    for (int i = 0; i < baton->function_start_addresses_count; i++)
  //    {
  //        printf ("0x%012llx\n", baton->function_start_addresses[i]);
  //    }

  //    printf ("symbol table names & addresses\n");
  //    for (int i = 0; i < baton->symbols_count; i++)
  //    {
  //        printf ("0x%012llx %s\n", baton->symbols[i].file_address,
  //        baton->symbols[i].name);
  //    }
}

void print_encoding_x86_64(struct baton baton, uint8_t *function_start,
                           uint32_t encoding) {
  int mode = encoding & UNWIND_X86_64_MODE_MASK;
  switch (mode) {
  case UNWIND_X86_64_MODE_RBP_FRAME: {
    printf("frame func: CFA is rbp+%d ", 16);
    printf(" rip=[CFA-8] rbp=[CFA-16]");
    uint32_t saved_registers_offset =
        EXTRACT_BITS(encoding, UNWIND_X86_64_RBP_FRAME_OFFSET);

    uint32_t saved_registers_locations =
        EXTRACT_BITS(encoding, UNWIND_X86_64_RBP_FRAME_REGISTERS);

    saved_registers_offset += 2;

    for (int i = 0; i < 5; i++) {
      switch (saved_registers_locations & 0x7) {
      case UNWIND_X86_64_REG_NONE:
        break;
      case UNWIND_X86_64_REG_RBX:
        printf(" rbx=[CFA-%d]", saved_registers_offset * 8);
        break;
      case UNWIND_X86_64_REG_R12:
        printf(" r12=[CFA-%d]", saved_registers_offset * 8);
        break;
      case UNWIND_X86_64_REG_R13:
        printf(" r13=[CFA-%d]", saved_registers_offset * 8);
        break;
      case UNWIND_X86_64_REG_R14:
        printf(" r14=[CFA-%d]", saved_registers_offset * 8);
        break;
      case UNWIND_X86_64_REG_R15:
        printf(" r15=[CFA-%d]", saved_registers_offset * 8);
        break;
      }
      saved_registers_offset--;
      saved_registers_locations >>= 3;
    }
  } break;

  case UNWIND_X86_64_MODE_STACK_IND:
  case UNWIND_X86_64_MODE_STACK_IMMD: {
    uint32_t stack_size =
        EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_SIZE);
    uint32_t register_count =
        EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT);
    uint32_t permutation =
        EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION);

    if (mode == UNWIND_X86_64_MODE_STACK_IND && function_start) {
      uint32_t stack_adjust =
          EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_ADJUST);

      // offset into the function instructions; 0 == beginning of first
      // instruction
      uint32_t offset_to_subl_insn =
          EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_SIZE);

      stack_size = *((uint32_t *)(function_start + offset_to_subl_insn));

      stack_size += stack_adjust * 8;

      printf("large stack ");
    }

    if (mode == UNWIND_X86_64_MODE_STACK_IND) {
      printf("frameless function: stack size %d, register count %d ",
             stack_size * 8, register_count);
    } else {
      printf("frameless function: stack size %d, register count %d ",
             stack_size, register_count);
    }

    if (register_count == 0) {
      printf(" no registers saved");
    } else {

      // We need to include (up to) 6 registers in 10 bits.
      // That would be 18 bits if we just used 3 bits per reg to indicate
      // the order they're saved on the stack.
      //
      // This is done with Lehmer code permutation, e.g. see
      // http://stackoverflow.com/questions/1506078/fast-permutation-number-permutation-mapping-algorithms
      int permunreg[6];

      // This decodes the variable-base number in the 10 bits
      // and gives us the Lehmer code sequence which can then
      // be decoded.

      switch (register_count) {
      case 6:
        permunreg[0] = permutation / 120; // 120 == 5!
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24; // 24 == 4!
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6; // 6 == 3!
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2; // 2 == 2!
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation; // 1 == 1!
        permunreg[5] = 0;
        break;
      case 5:
        permunreg[0] = permutation / 120;
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24;
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6;
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2;
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation;
        break;
      case 4:
        permunreg[0] = permutation / 60;
        permutation -= (permunreg[0] * 60);
        permunreg[1] = permutation / 12;
        permutation -= (permunreg[1] * 12);
        permunreg[2] = permutation / 3;
        permutation -= (permunreg[2] * 3);
        permunreg[3] = permutation;
        break;
      case 3:
        permunreg[0] = permutation / 20;
        permutation -= (permunreg[0] * 20);
        permunreg[1] = permutation / 4;
        permutation -= (permunreg[1] * 4);
        permunreg[2] = permutation;
        break;
      case 2:
        permunreg[0] = permutation / 5;
        permutation -= (permunreg[0] * 5);
        permunreg[1] = permutation;
        break;
      case 1:
        permunreg[0] = permutation;
        break;
      }

      // Decode the Lehmer code for this permutation of
      // the registers v. http://en.wikipedia.org/wiki/Lehmer_code

      int registers[6];
      bool used[7] = {false, false, false, false, false, false, false};
      for (int i = 0; i < register_count; i++) {
        int renum = 0;
        for (int j = 1; j < 7; j++) {
          if (used[j] == false) {
            if (renum == permunreg[i]) {
              registers[i] = j;
              used[j] = true;
              break;
            }
            renum++;
          }
        }
      }

      if (mode == UNWIND_X86_64_MODE_STACK_IND) {
        printf(" CFA is rsp+%d ", stack_size);
      } else {
        printf(" CFA is rsp+%d ", stack_size * 8);
      }

      uint32_t saved_registers_offset = 1;
      printf(" rip=[CFA-%d]", saved_registers_offset * 8);
      saved_registers_offset++;

      for (int i = (sizeof(registers) / sizeof(int)) - 1; i >= 0; i--) {
        switch (registers[i]) {
        case UNWIND_X86_64_REG_NONE:
          break;
        case UNWIND_X86_64_REG_RBX:
          printf(" rbx=[CFA-%d]", saved_registers_offset * 8);
          saved_registers_offset++;
          break;
        case UNWIND_X86_64_REG_R12:
          printf(" r12=[CFA-%d]", saved_registers_offset * 8);
          saved_registers_offset++;
          break;
        case UNWIND_X86_64_REG_R13:
          printf(" r13=[CFA-%d]", saved_registers_offset * 8);
          saved_registers_offset++;
          break;
        case UNWIND_X86_64_REG_R14:
          printf(" r14=[CFA-%d]", saved_registers_offset * 8);
          saved_registers_offset++;
          break;
        case UNWIND_X86_64_REG_R15:
          printf(" r15=[CFA-%d]", saved_registers_offset * 8);
          saved_registers_offset++;
          break;
        case UNWIND_X86_64_REG_RBP:
          printf(" rbp=[CFA-%d]", saved_registers_offset * 8);
          saved_registers_offset++;
          break;
        }
      }
    }

  } break;

  case UNWIND_X86_64_MODE_DWARF: {
    uint32_t dwarf_offset = encoding & UNWIND_X86_DWARF_SECTION_OFFSET;
    printf(
        "DWARF unwind instructions: FDE at offset %d (file address 0x%" PRIx64
        ")",
        dwarf_offset, dwarf_offset + baton.eh_section_file_address);
  } break;

  case 0: {
    printf(" no unwind information");
  } break;
  }
}

void print_encoding_i386(struct baton baton, uint8_t *function_start,
                         uint32_t encoding) {
  int mode = encoding & UNWIND_X86_MODE_MASK;
  switch (mode) {
  case UNWIND_X86_MODE_EBP_FRAME: {
    printf("frame func: CFA is ebp+%d ", 8);
    printf(" eip=[CFA-4] ebp=[CFA-8]");
    uint32_t saved_registers_offset =
        EXTRACT_BITS(encoding, UNWIND_X86_EBP_FRAME_OFFSET);

    uint32_t saved_registers_locations =
        EXTRACT_BITS(encoding, UNWIND_X86_EBP_FRAME_REGISTERS);

    saved_registers_offset += 2;

    for (int i = 0; i < 5; i++) {
      switch (saved_registers_locations & 0x7) {
      case UNWIND_X86_REG_NONE:
        break;
      case UNWIND_X86_REG_EBX:
        printf(" ebx=[CFA-%d]", saved_registers_offset * 4);
        break;
      case UNWIND_X86_REG_ECX:
        printf(" ecx=[CFA-%d]", saved_registers_offset * 4);
        break;
      case UNWIND_X86_REG_EDX:
        printf(" edx=[CFA-%d]", saved_registers_offset * 4);
        break;
      case UNWIND_X86_REG_EDI:
        printf(" edi=[CFA-%d]", saved_registers_offset * 4);
        break;
      case UNWIND_X86_REG_ESI:
        printf(" esi=[CFA-%d]", saved_registers_offset * 4);
        break;
      }
      saved_registers_offset--;
      saved_registers_locations >>= 3;
    }
  } break;

  case UNWIND_X86_MODE_STACK_IND:
  case UNWIND_X86_MODE_STACK_IMMD: {
    uint32_t stack_size =
        EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_SIZE);
    uint32_t register_count =
        EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_REG_COUNT);
    uint32_t permutation =
        EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_REG_PERMUTATION);

    if (mode == UNWIND_X86_MODE_STACK_IND && function_start) {
      uint32_t stack_adjust =
          EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_ADJUST);

      // offset into the function instructions; 0 == beginning of first
      // instruction
      uint32_t offset_to_subl_insn =
          EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_SIZE);

      stack_size = *((uint32_t *)(function_start + offset_to_subl_insn));

      stack_size += stack_adjust * 4;

      printf("large stack ");
    }

    if (mode == UNWIND_X86_MODE_STACK_IND) {
      printf("frameless function: stack size %d, register count %d ",
             stack_size, register_count);
    } else {
      printf("frameless function: stack size %d, register count %d ",
             stack_size * 4, register_count);
    }

    if (register_count == 0) {
      printf(" no registers saved");
    } else {

      // We need to include (up to) 6 registers in 10 bits.
      // That would be 18 bits if we just used 3 bits per reg to indicate
      // the order they're saved on the stack.
      //
      // This is done with Lehmer code permutation, e.g. see
      // http://stackoverflow.com/questions/1506078/fast-permutation-number-permutation-mapping-algorithms
      int permunreg[6];

      // This decodes the variable-base number in the 10 bits
      // and gives us the Lehmer code sequence which can then
      // be decoded.

      switch (register_count) {
      case 6:
        permunreg[0] = permutation / 120; // 120 == 5!
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24; // 24 == 4!
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6; // 6 == 3!
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2; // 2 == 2!
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation; // 1 == 1!
        permunreg[5] = 0;
        break;
      case 5:
        permunreg[0] = permutation / 120;
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24;
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6;
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2;
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation;
        break;
      case 4:
        permunreg[0] = permutation / 60;
        permutation -= (permunreg[0] * 60);
        permunreg[1] = permutation / 12;
        permutation -= (permunreg[1] * 12);
        permunreg[2] = permutation / 3;
        permutation -= (permunreg[2] * 3);
        permunreg[3] = permutation;
        break;
      case 3:
        permunreg[0] = permutation / 20;
        permutation -= (permunreg[0] * 20);
        permunreg[1] = permutation / 4;
        permutation -= (permunreg[1] * 4);
        permunreg[2] = permutation;
        break;
      case 2:
        permunreg[0] = permutation / 5;
        permutation -= (permunreg[0] * 5);
        permunreg[1] = permutation;
        break;
      case 1:
        permunreg[0] = permutation;
        break;
      }

      // Decode the Lehmer code for this permutation of
      // the registers v. http://en.wikipedia.org/wiki/Lehmer_code

      int registers[6];
      bool used[7] = {false, false, false, false, false, false, false};
      for (int i = 0; i < register_count; i++) {
        int renum = 0;
        for (int j = 1; j < 7; j++) {
          if (used[j] == false) {
            if (renum == permunreg[i]) {
              registers[i] = j;
              used[j] = true;
              break;
            }
            renum++;
          }
        }
      }

      if (mode == UNWIND_X86_MODE_STACK_IND) {
        printf(" CFA is esp+%d ", stack_size);
      } else {
        printf(" CFA is esp+%d ", stack_size * 4);
      }

      uint32_t saved_registers_offset = 1;
      printf(" eip=[CFA-%d]", saved_registers_offset * 4);
      saved_registers_offset++;

      for (int i = (sizeof(registers) / sizeof(int)) - 1; i >= 0; i--) {
        switch (registers[i]) {
        case UNWIND_X86_REG_NONE:
          break;
        case UNWIND_X86_REG_EBX:
          printf(" ebx=[CFA-%d]", saved_registers_offset * 4);
          saved_registers_offset++;
          break;
        case UNWIND_X86_REG_ECX:
          printf(" ecx=[CFA-%d]", saved_registers_offset * 4);
          saved_registers_offset++;
          break;
        case UNWIND_X86_REG_EDX:
          printf(" edx=[CFA-%d]", saved_registers_offset * 4);
          saved_registers_offset++;
          break;
        case UNWIND_X86_REG_EDI:
          printf(" edi=[CFA-%d]", saved_registers_offset * 4);
          saved_registers_offset++;
          break;
        case UNWIND_X86_REG_ESI:
          printf(" esi=[CFA-%d]", saved_registers_offset * 4);
          saved_registers_offset++;
          break;
        case UNWIND_X86_REG_EBP:
          printf(" ebp=[CFA-%d]", saved_registers_offset * 4);
          saved_registers_offset++;
          break;
        }
      }
    }

  } break;

  case UNWIND_X86_MODE_DWARF: {
    uint32_t dwarf_offset = encoding & UNWIND_X86_DWARF_SECTION_OFFSET;
    printf(
        "DWARF unwind instructions: FDE at offset %d (file address 0x%" PRIx64
        ")",
        dwarf_offset, dwarf_offset + baton.eh_section_file_address);
  } break;

  case 0: {
    printf(" no unwind information");
  } break;
  }
}

void print_encoding_arm64(struct baton baton, uint8_t *function_start,
                          uint32_t encoding) {
  const int wordsize = 8;
  int mode = encoding & UNWIND_ARM64_MODE_MASK;
  switch (mode) {
  case UNWIND_ARM64_MODE_FRAME: {
    printf("frame func: CFA is fp+%d ", 16);
    printf(" pc=[CFA-8] fp=[CFA-16]");
    int reg_pairs_saved_count = 1;
    uint32_t saved_register_bits = encoding & 0xfff;
    if (saved_register_bits & UNWIND_ARM64_FRAME_X19_X20_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" x19=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" x20=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_X21_X22_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" x21=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" x22=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_X23_X24_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" x23=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" x24=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_X25_X26_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" x25=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" x26=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_X27_X28_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" x27=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" x28=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_D8_D9_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" d8=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" d9=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_D10_D11_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" d10=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" d11=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_D12_D13_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" d12=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" d13=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }
    if (saved_register_bits & UNWIND_ARM64_FRAME_D14_D15_PAIR) {
      int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
      cfa_offset -= wordsize;
      printf(" d14=[CFA%d]", cfa_offset);
      cfa_offset -= wordsize;
      printf(" d15=[CFA%d]", cfa_offset);
      reg_pairs_saved_count++;
    }

  } break;

  case UNWIND_ARM64_MODE_FRAMELESS: {
    uint32_t stack_size = encoding & UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK;
    printf("frameless function: stack size %d ", stack_size * 16);

  } break;

  case UNWIND_ARM64_MODE_DWARF: {
    uint32_t dwarf_offset = encoding & UNWIND_ARM64_DWARF_SECTION_OFFSET;
    printf(
        "DWARF unwind instructions: FDE at offset %d (file address 0x%" PRIx64
        ")",
        dwarf_offset, dwarf_offset + baton.eh_section_file_address);
  } break;

  case 0: {
    printf(" no unwind information");
  } break;
  }
}

void print_encoding_armv7(struct baton baton, uint8_t *function_start,
                          uint32_t encoding) {
  const int wordsize = 4;
  int mode = encoding & UNWIND_ARM_MODE_MASK;
  switch (mode) {
  case UNWIND_ARM_MODE_FRAME_D:
  case UNWIND_ARM_MODE_FRAME: {
    int stack_adjust =
        EXTRACT_BITS(encoding, UNWIND_ARM_FRAME_STACK_ADJUST_MASK) * wordsize;

    printf("frame func: CFA is fp+%d ", (2 * wordsize) + stack_adjust);
    int cfa_offset = -stack_adjust;

    cfa_offset -= wordsize;
    printf(" pc=[CFA%d]", cfa_offset);
    cfa_offset -= wordsize;
    printf(" fp=[CFA%d]", cfa_offset);

    uint32_t saved_register_bits = encoding & 0xff;
    if (saved_register_bits & UNWIND_ARM_FRAME_FIRST_PUSH_R6) {
      cfa_offset -= wordsize;
      printf(" r6=[CFA%d]", cfa_offset);
    }
    if (saved_register_bits & UNWIND_ARM_FRAME_FIRST_PUSH_R5) {
      cfa_offset -= wordsize;
      printf(" r5=[CFA%d]", cfa_offset);
    }
    if (saved_register_bits & UNWIND_ARM_FRAME_FIRST_PUSH_R4) {
      cfa_offset -= wordsize;
      printf(" r4=[CFA%d]", cfa_offset);
    }
    if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R12) {
      cfa_offset -= wordsize;
      printf(" r12=[CFA%d]", cfa_offset);
    }
    if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R11) {
      cfa_offset -= wordsize;
      printf(" r11=[CFA%d]", cfa_offset);
    }
    if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R10) {
      cfa_offset -= wordsize;
      printf(" r10=[CFA%d]", cfa_offset);
    }
    if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R9) {
      cfa_offset -= wordsize;
      printf(" r9=[CFA%d]", cfa_offset);
    }
    if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R8) {
      cfa_offset -= wordsize;
      printf(" r8=[CFA%d]", cfa_offset);
    }

    if (mode == UNWIND_ARM_MODE_FRAME_D) {
      uint32_t d_reg_bits =
          EXTRACT_BITS(encoding, UNWIND_ARM_FRAME_D_REG_COUNT_MASK);
      switch (d_reg_bits) {
      case 0:
        // vpush {d8}
        cfa_offset -= 8;
        printf(" d8=[CFA%d]", cfa_offset);
        break;
      case 1:
        // vpush {d10}
        // vpush {d8}
        cfa_offset -= 8;
        printf(" d10=[CFA%d]", cfa_offset);
        cfa_offset -= 8;
        printf(" d8=[CFA%d]", cfa_offset);
        break;
      case 2:
        // vpush {d12}
        // vpush {d10}
        // vpush {d8}
        cfa_offset -= 8;
        printf(" d12=[CFA%d]", cfa_offset);
        cfa_offset -= 8;
        printf(" d10=[CFA%d]", cfa_offset);
        cfa_offset -= 8;
        printf(" d8=[CFA%d]", cfa_offset);
        break;
      case 3:
        // vpush {d14}
        // vpush {d12}
        // vpush {d10}
        // vpush {d8}
        cfa_offset -= 8;
        printf(" d14=[CFA%d]", cfa_offset);
        cfa_offset -= 8;
        printf(" d12=[CFA%d]", cfa_offset);
        cfa_offset -= 8;
        printf(" d10=[CFA%d]", cfa_offset);
        cfa_offset -= 8;
        printf(" d8=[CFA%d]", cfa_offset);
        break;
      case 4:
        // vpush {d14}
        // vpush {d12}
        // sp = (sp - 24) & (-16);
        // vst   {d8, d9, d10}
        printf(" d14, d12, d10, d9, d8");
        break;
      case 5:
        // vpush {d14}
        // sp = (sp - 40) & (-16);
        // vst   {d8, d9, d10, d11}
        // vst   {d12}
        printf(" d14, d11, d10, d9, d8, d12");
        break;
      case 6:
        // sp = (sp - 56) & (-16);
        // vst   {d8, d9, d10, d11}
        // vst   {d12, d13, d14}
        printf(" d11, d10, d9, d8, d14, d13, d12");
        break;
      case 7:
        // sp = (sp - 64) & (-16);
        // vst   {d8, d9, d10, d11}
        // vst   {d12, d13, d14, d15}
        printf(" d11, d10, d9, d8, d15, d14, d13, d12");
        break;
      }
    }
  } break;

  case UNWIND_ARM_MODE_DWARF: {
    uint32_t dwarf_offset = encoding & UNWIND_ARM_DWARF_SECTION_OFFSET;
    printf(
        "DWARF unwind instructions: FDE at offset %d (file address 0x%" PRIx64
        ")",
        dwarf_offset, dwarf_offset + baton.eh_section_file_address);
  } break;

  case 0: {
    printf(" no unwind information");
  } break;
  }
}

void print_encoding(struct baton baton, uint8_t *function_start,
                    uint32_t encoding) {

  if (baton.cputype == CPU_TYPE_X86_64) {
    print_encoding_x86_64(baton, function_start, encoding);
  } else if (baton.cputype == CPU_TYPE_I386) {
    print_encoding_i386(baton, function_start, encoding);
  } else if (baton.cputype == CPU_TYPE_ARM64) {
    print_encoding_arm64(baton, function_start, encoding);
  } else if (baton.cputype == CPU_TYPE_ARM) {
    print_encoding_armv7(baton, function_start, encoding);
  } else {
    printf(" -- unsupported encoding arch -- ");
  }
}

void print_function_encoding(struct baton baton, uint32_t idx,
                             uint32_t encoding, uint32_t entry_encoding_index,
                             uint32_t entry_func_offset) {

  char *entry_encoding_index_str = "";
  if (entry_encoding_index != (uint32_t)-1) {
    asprintf(&entry_encoding_index_str, ", encoding #%d", entry_encoding_index);
  } else {
    asprintf(&entry_encoding_index_str, "");
  }

  uint64_t file_address = baton.first_level_index_entry.functionOffset +
                          entry_func_offset + baton.text_segment_vmaddr;

  if (baton.cputype == CPU_TYPE_ARM)
    file_address = file_address & ~1;

  printf(
      "    func [%d] offset %d (file addr 0x%" PRIx64 ")%s, encoding is 0x%x",
      idx, entry_func_offset, file_address, entry_encoding_index_str, encoding);

  struct symbol *symbol = NULL;
  for (int i = 0; i < baton.symbols_count; i++) {
    if (i == baton.symbols_count - 1 &&
        baton.symbols[i].file_address <= file_address) {
      symbol = &(baton.symbols[i]);
      break;
    } else {
      if (baton.symbols[i].file_address <= file_address &&
          baton.symbols[i + 1].file_address > file_address) {
        symbol = &(baton.symbols[i]);
        break;
      }
    }
  }

  printf("\n         ");
  if (symbol) {
    int offset = file_address - symbol->file_address;

    // FIXME this is a poor heuristic - if we're greater than 16 bytes past the
    // start of the function, this is the unwind info for a stripped function.
    // In reality the compact unwind entry may not line up exactly with the
    // function bounds.
    if (offset >= 0) {
      printf("name: %s", symbol->name);
      if (offset > 0) {
        printf(" + %d", offset);
      }
    }
    printf("\n         ");
  }

  print_encoding(baton, baton.mach_header_start +
                            baton.first_level_index_entry.functionOffset +
                            baton.text_section_file_offset + entry_func_offset,
                 encoding);

  bool has_lsda = encoding & UNWIND_HAS_LSDA;

  if (has_lsda) {
    uint32_t func_offset =
        entry_func_offset + baton.first_level_index_entry.functionOffset;

    int lsda_entry_number = -1;

    uint32_t low = 0;
    uint32_t high = (baton.lsda_array_end - baton.lsda_array_start) /
                    sizeof(struct unwind_info_section_header_lsda_index_entry);

    while (low < high) {
      uint32_t mid = (low + high) / 2;

      uint8_t *mid_lsda_entry_addr =
          (baton.lsda_array_start +
           (mid * sizeof(struct unwind_info_section_header_lsda_index_entry)));
      struct unwind_info_section_header_lsda_index_entry mid_lsda_entry;
      memcpy(&mid_lsda_entry, mid_lsda_entry_addr,
             sizeof(struct unwind_info_section_header_lsda_index_entry));
      if (mid_lsda_entry.functionOffset == func_offset) {
        lsda_entry_number =
            (mid_lsda_entry_addr - baton.lsda_array_start) /
            sizeof(struct unwind_info_section_header_lsda_index_entry);
        break;
      } else if (mid_lsda_entry.functionOffset < func_offset) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }

    if (lsda_entry_number != -1) {
      printf(", LSDA entry #%d", lsda_entry_number);
    } else {
      printf(", LSDA entry not found");
    }
  }

  uint32_t pers_idx = EXTRACT_BITS(encoding, UNWIND_PERSONALITY_MASK);
  if (pers_idx != 0) {
    pers_idx--; // Change 1-based to 0-based index
    printf(", personality entry #%d", pers_idx);
  }

  printf("\n");
}

void print_second_level_index_regular(struct baton baton) {
  uint8_t *page_entries =
      baton.compact_unwind_start +
      baton.first_level_index_entry.secondLevelPagesSectionOffset +
      baton.regular_second_level_page_header.entryPageOffset;
  uint32_t entries_count = baton.regular_second_level_page_header.entryCount;

  uint8_t *offset = page_entries;

  uint32_t idx = 0;
  while (idx < entries_count) {
    uint32_t func_offset = *((uint32_t *)(offset));
    uint32_t encoding = *((uint32_t *)(offset + 4));

    // UNWIND_SECOND_LEVEL_REGULAR entries have a funcOffset which includes the
    // functionOffset from the containing index table already.
    // UNWIND_SECOND_LEVEL_COMPRESSED
    // entries only have the offset from the containing index table
    // functionOffset.
    // So strip off the containing index table functionOffset value here so they
    // can
    // be treated the same at the lower layers.

    print_function_encoding(baton, idx, encoding, (uint32_t)-1,
                            func_offset -
                                baton.first_level_index_entry.functionOffset);
    idx++;
    offset += 8;
  }
}

void print_second_level_index_compressed(struct baton baton) {
  uint8_t *this_index =
      baton.compact_unwind_start +
      baton.first_level_index_entry.secondLevelPagesSectionOffset;
  uint8_t *start_of_entries =
      this_index + baton.compressed_second_level_page_header.entryPageOffset;
  uint8_t *offset = start_of_entries;
  for (uint16_t idx = 0;
       idx < baton.compressed_second_level_page_header.entryCount; idx++) {
    uint32_t entry = *((uint32_t *)offset);
    offset += 4;
    uint32_t encoding;

    uint32_t entry_encoding_index =
        UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(entry);
    uint32_t entry_func_offset =
        UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(entry);

    if (entry_encoding_index < baton.unwind_header.commonEncodingsArrayCount) {
      // encoding is in common table in section header
      encoding =
          *((uint32_t *)(baton.compact_unwind_start +
                         baton.unwind_header.commonEncodingsArraySectionOffset +
                         (entry_encoding_index * sizeof(uint32_t))));
    } else {
      // encoding is in page specific table
      uint32_t page_encoding_index =
          entry_encoding_index - baton.unwind_header.commonEncodingsArrayCount;
      encoding = *((uint32_t *)(this_index +
                                baton.compressed_second_level_page_header
                                    .encodingsPageOffset +
                                (page_encoding_index * sizeof(uint32_t))));
    }

    print_function_encoding(baton, idx, encoding, entry_encoding_index,
                            entry_func_offset);
  }
}

void print_second_level_index(struct baton baton) {
  uint8_t *index_start =
      baton.compact_unwind_start +
      baton.first_level_index_entry.secondLevelPagesSectionOffset;

  if ((*(uint32_t *)index_start) == UNWIND_SECOND_LEVEL_REGULAR) {
    struct unwind_info_regular_second_level_page_header header;
    memcpy(&header, index_start,
           sizeof(struct unwind_info_regular_second_level_page_header));
    printf(
        "  UNWIND_SECOND_LEVEL_REGULAR #%d entryPageOffset %d, entryCount %d\n",
        baton.current_index_table_number, header.entryPageOffset,
        header.entryCount);
    baton.regular_second_level_page_header = header;
    print_second_level_index_regular(baton);
  }

  if ((*(uint32_t *)index_start) == UNWIND_SECOND_LEVEL_COMPRESSED) {
    struct unwind_info_compressed_second_level_page_header header;
    memcpy(&header, index_start,
           sizeof(struct unwind_info_compressed_second_level_page_header));
    printf("  UNWIND_SECOND_LEVEL_COMPRESSED #%d entryPageOffset %d, "
           "entryCount %d, encodingsPageOffset %d, encodingsCount %d\n",
           baton.current_index_table_number, header.entryPageOffset,
           header.entryCount, header.encodingsPageOffset,
           header.encodingsCount);
    baton.compressed_second_level_page_header = header;
    print_second_level_index_compressed(baton);
  }
}

void print_index_sections(struct baton baton) {
  uint8_t *index_section_offset =
      baton.compact_unwind_start + baton.unwind_header.indexSectionOffset;
  uint32_t index_count = baton.unwind_header.indexCount;

  uint32_t cur_idx = 0;

  uint8_t *offset = index_section_offset;
  while (cur_idx < index_count) {
    baton.current_index_table_number = cur_idx;
    struct unwind_info_section_header_index_entry index_entry;
    memcpy(&index_entry, offset,
           sizeof(struct unwind_info_section_header_index_entry));
    printf("index section #%d: functionOffset %d, "
           "secondLevelPagesSectionOffset %d, lsdaIndexArraySectionOffset %d\n",
           cur_idx, index_entry.functionOffset,
           index_entry.secondLevelPagesSectionOffset,
           index_entry.lsdaIndexArraySectionOffset);

    // secondLevelPagesSectionOffset == 0 means this is a sentinel entry
    if (index_entry.secondLevelPagesSectionOffset != 0) {
      struct unwind_info_section_header_index_entry next_index_entry;
      memcpy(&next_index_entry,
             offset + sizeof(struct unwind_info_section_header_index_entry),
             sizeof(struct unwind_info_section_header_index_entry));

      baton.lsda_array_start =
          baton.compact_unwind_start + index_entry.lsdaIndexArraySectionOffset;
      baton.lsda_array_end = baton.compact_unwind_start +
                             next_index_entry.lsdaIndexArraySectionOffset;

      uint8_t *lsda_entry_offset = baton.lsda_array_start;
      uint32_t lsda_count = 0;
      while (lsda_entry_offset < baton.lsda_array_end) {
        struct unwind_info_section_header_lsda_index_entry lsda_entry;
        memcpy(&lsda_entry, lsda_entry_offset,
               sizeof(struct unwind_info_section_header_lsda_index_entry));
        uint64_t function_file_address =
            baton.first_level_index_entry.functionOffset +
            lsda_entry.functionOffset + baton.text_segment_vmaddr;
        uint64_t lsda_file_address =
            lsda_entry.lsdaOffset + baton.text_segment_vmaddr;
        printf("    LSDA [%d] functionOffset %d (%d) (file address 0x%" PRIx64
               "), lsdaOffset %d (file address 0x%" PRIx64 ")\n",
               lsda_count, lsda_entry.functionOffset,
               lsda_entry.functionOffset - index_entry.functionOffset,
               function_file_address, lsda_entry.lsdaOffset, lsda_file_address);
        lsda_count++;
        lsda_entry_offset +=
            sizeof(struct unwind_info_section_header_lsda_index_entry);
      }

      printf("\n");

      baton.first_level_index_entry = index_entry;
      print_second_level_index(baton);
    }

    printf("\n");

    cur_idx++;
    offset += sizeof(struct unwind_info_section_header_index_entry);
  }
}

int main(int argc, char **argv) {
  struct stat st;
  char *file = argv[0];
  if (argc > 1)
    file = argv[1];
  int fd = open(file, O_RDONLY);
  if (fd == -1) {
    printf("Failed to open '%s'\n", file);
    exit(1);
  }
  fstat(fd, &st);
  uint8_t *file_mem =
      (uint8_t *)mmap(0, st.st_size, PROT_READ, MAP_PRIVATE | MAP_FILE, fd, 0);
  if (file_mem == MAP_FAILED) {
    printf("Failed to mmap() '%s'\n", file);
  }

  FILE *f = fopen("a.out", "r");

  struct baton baton;
  baton.mach_header_start = file_mem;
  baton.symbols = NULL;
  baton.symbols_count = 0;
  baton.function_start_addresses = NULL;
  baton.function_start_addresses_count = 0;

  scan_macho_load_commands(&baton);

  if (baton.compact_unwind_start == NULL) {
    printf("could not find __TEXT,__unwind_info section\n");
    exit(1);
  }

  struct unwind_info_section_header header;
  memcpy(&header, baton.compact_unwind_start,
         sizeof(struct unwind_info_section_header));
  printf("Header:\n");
  printf("  version %u\n", header.version);
  printf("  commonEncodingsArraySectionOffset is %d\n",
         header.commonEncodingsArraySectionOffset);
  printf("  commonEncodingsArrayCount is %d\n",
         header.commonEncodingsArrayCount);
  printf("  personalityArraySectionOffset is %d\n",
         header.personalityArraySectionOffset);
  printf("  personalityArrayCount is %d\n", header.personalityArrayCount);
  printf("  indexSectionOffset is %d\n", header.indexSectionOffset);
  printf("  indexCount is %d\n", header.indexCount);

  uint8_t *common_encodings =
      baton.compact_unwind_start + header.commonEncodingsArraySectionOffset;
  uint32_t encoding_idx = 0;
  while (encoding_idx < header.commonEncodingsArrayCount) {
    uint32_t encoding = *((uint32_t *)common_encodings);
    printf("    Common Encoding [%d]: 0x%x ", encoding_idx, encoding);
    print_encoding(baton, NULL, encoding);
    printf("\n");
    common_encodings += sizeof(uint32_t);
    encoding_idx++;
  }

  uint8_t *pers_arr =
      baton.compact_unwind_start + header.personalityArraySectionOffset;
  uint32_t pers_idx = 0;
  while (pers_idx < header.personalityArrayCount) {
    int32_t pers_delta = *((int32_t *)(baton.compact_unwind_start +
                                       header.personalityArraySectionOffset +
                                       (pers_idx * sizeof(uint32_t))));
    printf("    Personality [%d]: personality function ptr @ offset %d (file "
           "address 0x%" PRIx64 ")\n",
           pers_idx, pers_delta, baton.text_segment_vmaddr + pers_delta);
    pers_idx++;
    pers_arr += sizeof(uint32_t);
  }

  printf("\n");

  baton.unwind_header = header;

  print_index_sections(baton);

  return 0;
}
