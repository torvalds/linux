/*
 * kmp_utility.cpp -- Utility routines for the OpenMP support library.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"
#include "kmp_i18n.h"
#include "kmp_str.h"
#include "kmp_wrapper_getpid.h"
#include <float.h>

static const char *unknown = "unknown";

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

/* NOTE: If called before serial_initialize (i.e. from runtime_initialize), then
   the debugging package has not been initialized yet, and only "0" will print
   debugging output since the environment variables have not been read. */

#ifdef KMP_DEBUG
static int trace_level = 5;
#endif

/* LOG_ID_BITS  = ( 1 + floor( log_2( max( log_per_phy - 1, 1 ))))
 * APIC_ID      = (PHY_ID << LOG_ID_BITS) | LOG_ID
 * PHY_ID       = APIC_ID >> LOG_ID_BITS
 */
int __kmp_get_physical_id(int log_per_phy, int apic_id) {
  int index_lsb, index_msb, temp;

  if (log_per_phy > 1) {
    index_lsb = 0;
    index_msb = 31;

    temp = log_per_phy;
    while ((temp & 1) == 0) {
      temp >>= 1;
      index_lsb++;
    }

    temp = log_per_phy;
    while ((temp & 0x80000000) == 0) {
      temp <<= 1;
      index_msb--;
    }

    /* If >1 bits were set in log_per_phy, choose next higher power of 2 */
    if (index_lsb != index_msb)
      index_msb++;

    return ((int)(apic_id >> index_msb));
  }

  return apic_id;
}

/*
 * LOG_ID_BITS  = ( 1 + floor( log_2( max( log_per_phy - 1, 1 ))))
 * APIC_ID      = (PHY_ID << LOG_ID_BITS) | LOG_ID
 * LOG_ID       = APIC_ID & (( 1 << LOG_ID_BITS ) - 1 )
 */
int __kmp_get_logical_id(int log_per_phy, int apic_id) {
  unsigned current_bit;
  int bits_seen;

  if (log_per_phy <= 1)
    return (0);

  bits_seen = 0;

  for (current_bit = 1; log_per_phy != 0; current_bit <<= 1) {
    if (log_per_phy & current_bit) {
      log_per_phy &= ~current_bit;
      bits_seen++;
    }
  }

  /* If exactly 1 bit was set in log_per_phy, choose next lower power of 2 */
  if (bits_seen == 1) {
    current_bit >>= 1;
  }

  return ((int)((current_bit - 1) & apic_id));
}

static kmp_uint64 __kmp_parse_frequency( // R: Frequency in Hz.
    char const *frequency // I: Float number and unit: MHz, GHz, or TGz.
    ) {

  double value = 0.0;
  char *unit = NULL;
  kmp_uint64 result = 0; /* Zero is a better unknown value than all ones. */

  if (frequency == NULL) {
    return result;
  }
  value = strtod(frequency, &unit);
  if (0 < value &&
      value <= DBL_MAX) { // Good value (not overflow, underflow, etc).
    if (strcmp(unit, "MHz") == 0) {
      value = value * 1.0E+6;
    } else if (strcmp(unit, "GHz") == 0) {
      value = value * 1.0E+9;
    } else if (strcmp(unit, "THz") == 0) {
      value = value * 1.0E+12;
    } else { // Wrong unit.
      return result;
    }
    result = value;
  }
  return result;

} // func __kmp_parse_cpu_frequency

void __kmp_query_cpuid(kmp_cpuinfo_t *p) {
  struct kmp_cpuid buf;
  int max_arg;
  int log_per_phy;
#ifdef KMP_DEBUG
  int cflush_size;
#endif

  p->initialized = 1;

  p->sse2 = 1; // Assume SSE2 by default.

  __kmp_x86_cpuid(0, 0, &buf);

  KA_TRACE(trace_level,
           ("INFO: CPUID %d: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X\n", 0,
            buf.eax, buf.ebx, buf.ecx, buf.edx));

  max_arg = buf.eax;

  p->apic_id = -1;

  if (max_arg >= 1) {
    int i;
    kmp_uint32 t, data[4];

    __kmp_x86_cpuid(1, 0, &buf);
    KA_TRACE(trace_level,
             ("INFO: CPUID %d: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X\n",
              1, buf.eax, buf.ebx, buf.ecx, buf.edx));

    {
#define get_value(reg, lo, mask) (((reg) >> (lo)) & (mask))

      p->signature = buf.eax;
      p->family = get_value(buf.eax, 20, 0xff) + get_value(buf.eax, 8, 0x0f);
      p->model =
          (get_value(buf.eax, 16, 0x0f) << 4) + get_value(buf.eax, 4, 0x0f);
      p->stepping = get_value(buf.eax, 0, 0x0f);

#undef get_value

      KA_TRACE(trace_level, (" family = %d, model = %d, stepping = %d\n",
                             p->family, p->model, p->stepping));
    }

    for (t = buf.ebx, i = 0; i < 4; t >>= 8, ++i) {
      data[i] = (t & 0xff);
    }

    p->sse2 = (buf.edx >> 26) & 1;

#ifdef KMP_DEBUG

    if ((buf.edx >> 4) & 1) {
      /* TSC - Timestamp Counter Available */
      KA_TRACE(trace_level, (" TSC"));
    }
    if ((buf.edx >> 8) & 1) {
      /* CX8 - CMPXCHG8B Instruction Available */
      KA_TRACE(trace_level, (" CX8"));
    }
    if ((buf.edx >> 9) & 1) {
      /* APIC - Local APIC Present (multi-processor operation support */
      KA_TRACE(trace_level, (" APIC"));
    }
    if ((buf.edx >> 15) & 1) {
      /* CMOV - Conditional MOVe Instruction Available */
      KA_TRACE(trace_level, (" CMOV"));
    }
    if ((buf.edx >> 18) & 1) {
      /* PSN - Processor Serial Number Available */
      KA_TRACE(trace_level, (" PSN"));
    }
    if ((buf.edx >> 19) & 1) {
      /* CLFULSH - Cache Flush Instruction Available */
      cflush_size =
          data[1] * 8; /* Bits 15-08: CLFLUSH line size = 8 (64 bytes) */
      KA_TRACE(trace_level, (" CLFLUSH(%db)", cflush_size));
    }
    if ((buf.edx >> 21) & 1) {
      /* DTES - Debug Trace & EMON Store */
      KA_TRACE(trace_level, (" DTES"));
    }
    if ((buf.edx >> 22) & 1) {
      /* ACPI - ACPI Support Available */
      KA_TRACE(trace_level, (" ACPI"));
    }
    if ((buf.edx >> 23) & 1) {
      /* MMX - Multimedia Extensions */
      KA_TRACE(trace_level, (" MMX"));
    }
    if ((buf.edx >> 25) & 1) {
      /* SSE - SSE Instructions */
      KA_TRACE(trace_level, (" SSE"));
    }
    if ((buf.edx >> 26) & 1) {
      /* SSE2 - SSE2 Instructions */
      KA_TRACE(trace_level, (" SSE2"));
    }
    if ((buf.edx >> 27) & 1) {
      /* SLFSNP - Self-Snooping Cache */
      KA_TRACE(trace_level, (" SLFSNP"));
    }
#endif /* KMP_DEBUG */

    if ((buf.edx >> 28) & 1) {
      /* Bits 23-16: Logical Processors per Physical Processor (1 for P4) */
      log_per_phy = data[2];
      p->apic_id = data[3]; /* Bits 31-24: Processor Initial APIC ID (X) */
      KA_TRACE(trace_level, (" HT(%d TPUs)", log_per_phy));

      if (log_per_phy > 1) {
/* default to 1k FOR JT-enabled processors (4k on OS X*) */
#if KMP_OS_DARWIN
        p->cpu_stackoffset = 4 * 1024;
#else
        p->cpu_stackoffset = 1 * 1024;
#endif
      }

      p->physical_id = __kmp_get_physical_id(log_per_phy, p->apic_id);
      p->logical_id = __kmp_get_logical_id(log_per_phy, p->apic_id);
    }
#ifdef KMP_DEBUG
    if ((buf.edx >> 29) & 1) {
      /* ATHROTL - Automatic Throttle Control */
      KA_TRACE(trace_level, (" ATHROTL"));
    }
    KA_TRACE(trace_level, (" ]\n"));

    for (i = 2; i <= max_arg; ++i) {
      __kmp_x86_cpuid(i, 0, &buf);
      KA_TRACE(trace_level,
               ("INFO: CPUID %d: EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X\n",
                i, buf.eax, buf.ebx, buf.ecx, buf.edx));
    }
#endif
#if KMP_USE_ADAPTIVE_LOCKS
    p->rtm = 0;
    if (max_arg > 7) {
      /* RTM bit CPUID.07:EBX, bit 11 */
      __kmp_x86_cpuid(7, 0, &buf);
      p->rtm = (buf.ebx >> 11) & 1;
      KA_TRACE(trace_level, (" RTM"));
    }
#endif
  }

  { // Parse CPU brand string for frequency, saving the string for later.
    int i;
    kmp_cpuid_t *base = (kmp_cpuid_t *)&p->name[0];

    // Get CPU brand string.
    for (i = 0; i < 3; ++i) {
      __kmp_x86_cpuid(0x80000002 + i, 0, base + i);
    }
    p->name[sizeof(p->name) - 1] = 0; // Just in case. ;-)
    KA_TRACE(trace_level, ("cpu brand string: \"%s\"\n", &p->name[0]));

    // Parse frequency.
    p->frequency = __kmp_parse_frequency(strrchr(&p->name[0], ' '));
    KA_TRACE(trace_level,
             ("cpu frequency from brand string: %" KMP_UINT64_SPEC "\n",
              p->frequency));
  }
}

#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

void __kmp_expand_host_name(char *buffer, size_t size) {
  KMP_DEBUG_ASSERT(size >= sizeof(unknown));
#if KMP_OS_WINDOWS
  {
    DWORD s = size;

    if (!GetComputerNameA(buffer, &s))
      KMP_STRCPY_S(buffer, size, unknown);
  }
#else
  buffer[size - 2] = 0;
  if (gethostname(buffer, size) || buffer[size - 2] != 0)
    KMP_STRCPY_S(buffer, size, unknown);
#endif
}

/* Expand the meta characters in the filename:
 * Currently defined characters are:
 * %H the hostname
 * %P the number of threads used.
 * %I the unique identifier for this run.
 */

void __kmp_expand_file_name(char *result, size_t rlen, char *pattern) {
  char *pos = result, *end = result + rlen - 1;
  char buffer[256];
  int default_cpu_width = 1;
  int snp_result;

  KMP_DEBUG_ASSERT(rlen > 0);
  *end = 0;
  {
    int i;
    for (i = __kmp_xproc; i >= 10; i /= 10, ++default_cpu_width)
      ;
  }

  if (pattern != NULL) {
    while (*pattern != '\0' && pos < end) {
      if (*pattern != '%') {
        *pos++ = *pattern++;
      } else {
        char *old_pattern = pattern;
        int width = 1;
        int cpu_width = default_cpu_width;

        ++pattern;

        if (*pattern >= '0' && *pattern <= '9') {
          width = 0;
          do {
            width = (width * 10) + *pattern++ - '0';
          } while (*pattern >= '0' && *pattern <= '9');
          if (width < 0 || width > 1024)
            width = 1;

          cpu_width = width;
        }

        switch (*pattern) {
        case 'H':
        case 'h': {
          __kmp_expand_host_name(buffer, sizeof(buffer));
          KMP_STRNCPY(pos, buffer, end - pos + 1);
          if (*end == 0) {
            while (*pos)
              ++pos;
            ++pattern;
          } else
            pos = end;
        } break;
        case 'P':
        case 'p': {
          snp_result = KMP_SNPRINTF(pos, end - pos + 1, "%0*d", cpu_width,
                                    __kmp_dflt_team_nth);
          if (snp_result >= 0 && snp_result <= end - pos) {
            while (*pos)
              ++pos;
            ++pattern;
          } else
            pos = end;
        } break;
        case 'I':
        case 'i': {
          pid_t id = getpid();
#if KMP_ARCH_X86_64 && defined(__MINGW32__)
          snp_result = KMP_SNPRINTF(pos, end - pos + 1, "%0*lld", width, id);
#else
          snp_result = KMP_SNPRINTF(pos, end - pos + 1, "%0*d", width, id);
#endif
          if (snp_result >= 0 && snp_result <= end - pos) {
            while (*pos)
              ++pos;
            ++pattern;
          } else
            pos = end;
          break;
        }
        case '%': {
          *pos++ = '%';
          ++pattern;
          break;
        }
        default: {
          *pos++ = '%';
          pattern = old_pattern + 1;
          break;
        }
        }
      }
    }
    /* TODO: How do we get rid of this? */
    if (*pattern != '\0')
      KMP_FATAL(FileNameTooLong);
  }

  *pos = '\0';
}
