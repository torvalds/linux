/* =========================================================================
    Unity Project - A Test Framework for C
    Copyright (c) 2007-14 Mike Karlesky, Mark VanderVoord, Greg Williams
    [Released under MIT License. Please refer to license.txt for details]
============================================================================ */

#include "unity.h"

#define UNITY_FAIL_AND_BAIL   { Unity.CurrentTestFailed  = 1; longjmp(Unity.AbortFrame, 1); }
#define UNITY_IGNORE_AND_BAIL { Unity.CurrentTestIgnored = 1; longjmp(Unity.AbortFrame, 1); }
/// return prematurely if we are already in failure or ignore state
#define UNITY_SKIP_EXECUTION  { if ((Unity.CurrentTestFailed != 0) || (Unity.CurrentTestIgnored != 0)) {return;} }
#define UNITY_PRINT_EOL       { UNITY_OUTPUT_CHAR('\n'); }

struct _Unity Unity;

const char UnityStrOk[]                     = "OK";
const char UnityStrPass[]                   = "PASS";
const char UnityStrFail[]                   = "FAIL";
const char UnityStrIgnore[]                 = "IGNORE";
const char UnityStrXPASS[]                  = "XPASS";
const char UnityStrXFAIL[]                  = "XFAIL";
const char UnityStrNull[]                   = "NULL";
const char UnityStrSpacer[]                 = ". ";
const char UnityStrExpected[]               = " Expected ";
const char UnityStrWas[]                    = " Was ";
const char UnityStrTo[]                     = " To ";
const char UnityStrElement[]                = " Element ";
const char UnityStrByte[]                   = " Byte ";
const char UnityStrMemory[]                 = " Memory Mismatch.";
const char UnityStrDelta[]                  = " Values Not Within Delta ";
const char UnityStrPointless[]              = " You Asked Me To Compare Nothing, Which Was Pointless.";
const char UnityStrNullPointerForExpected[] = " Expected pointer to be NULL";
const char UnityStrNullPointerForActual[]   = " Actual pointer was NULL";
const char UnityStrNot[]                    = "Not ";
const char UnityStrInf[]                    = "Infinity";
const char UnityStrNegInf[]                 = "Negative Infinity";
const char UnityStrNaN[]                    = "NaN";
const char UnityStrDet[]                    = "Determinate";
const char UnityStrErrFloat[]               = "Unity Floating Point Disabled";
const char UnityStrErrDouble[]              = "Unity Double Precision Disabled";
const char UnityStrErr64[]                  = "Unity 64-bit Support Disabled";
const char UnityStrBreaker[]                = "-----------------------";
const char UnityStrResultsTests[]           = " Tests: ";
const char UnityStrResultsFailures[]        = " Failures ";
const char UnityStrResultsIgnored[]         = " Ignored ";
const char UnityStrResultsXFAIL[]           = " XFAIL ";
const char UnityStrResultsXPASS[]           = " XPASS ";
const char UnityStrResultsPass[]            = " PASS ";


#ifndef UNITY_EXCLUDE_FLOAT
// Dividing by these constants produces +/- infinity.
// The rationale is given in UnityAssertFloatIsInf's body.
static const _UF f_zero = 0.0f;
#ifndef UNITY_EXCLUDE_DOUBLE
static const _UD d_zero = 0.0;
#endif
#endif

// compiler-generic print formatting masks
const _U_UINT UnitySizeMask[] =
{
    255u,         // 0xFF
    65535u,       // 0xFFFF
    65535u,
    4294967295u,  // 0xFFFFFFFF
    4294967295u,
    4294967295u,
    4294967295u
#ifdef UNITY_SUPPORT_64
    ,0xFFFFFFFFFFFFFFFF
#endif
};

void UnityPrintFail(void);
void UnityPrintOk(void);

//-----------------------------------------------
// Pretty Printers & Test Result Output Handlers
//-----------------------------------------------

void UnityPrint(const char* string)
{
    const char* pch = string;

    if (pch != NULL)
    {
        while (*pch)
        {
            // printable characters plus CR & LF are printed
            if ((*pch <= 126) && (*pch >= 32))
            {
                UNITY_OUTPUT_CHAR(*pch);
            }
            //write escaped carriage returns
            else if (*pch == 13)
            {
                UNITY_OUTPUT_CHAR('\\');
                UNITY_OUTPUT_CHAR('r');
            }
            //write escaped line feeds
            else if (*pch == 10)
            {
                UNITY_OUTPUT_CHAR('\\');
                UNITY_OUTPUT_CHAR('n');
            }
            // unprintable characters are shown as codes
            else
            {
                UNITY_OUTPUT_CHAR('\\');
                UnityPrintNumberHex((_U_UINT)*pch, 2);
            }
            pch++;
        }
    }
}

//-----------------------------------------------
void UnityPrintNumberByStyle(const _U_SINT number, const UNITY_DISPLAY_STYLE_T style)
{
    if ((style & UNITY_DISPLAY_RANGE_INT) == UNITY_DISPLAY_RANGE_INT)
    {
        UnityPrintNumber(number);
    }
    else if ((style & UNITY_DISPLAY_RANGE_UINT) == UNITY_DISPLAY_RANGE_UINT)
    {
        UnityPrintNumberUnsigned(  (_U_UINT)number  &  UnitySizeMask[((_U_UINT)style & (_U_UINT)0x0F) - 1]  );
    }
    else
    {
        UnityPrintNumberHex((_U_UINT)number, (char)((style & 0x000F) << 1));
    }
}

//-----------------------------------------------
/// basically do an itoa using as little ram as possible
void UnityPrintNumber(const _U_SINT number_to_print)
{
    _U_SINT divisor = 1;
    _U_SINT next_divisor;
    _U_UINT number;

    if (number_to_print == (1l << (UNITY_LONG_WIDTH-1)))
    {
        //The largest representable negative number
        UNITY_OUTPUT_CHAR('-');
        number = (1ul << (UNITY_LONG_WIDTH-1));
    }
    else if (number_to_print < 0)
    {
        //Some other negative number
        UNITY_OUTPUT_CHAR('-');
        number = (_U_UINT)(-number_to_print);
    }
    else
    {
        //Positive number
        number = (_U_UINT)number_to_print;
    }

    // figure out initial divisor
    while (number / divisor > 9)
    {
        next_divisor = divisor * 10;
        if (next_divisor > divisor)
            divisor = next_divisor;
        else
            break;
    }

    // now mod and print, then divide divisor
    do
    {
        UNITY_OUTPUT_CHAR((char)('0' + (number / divisor % 10)));
        divisor /= 10;
    }
    while (divisor > 0);
}

//-----------------------------------------------
/// basically do an itoa using as little ram as possible
void UnityPrintNumberUnsigned(const _U_UINT number)
{
    _U_UINT divisor = 1;
    _U_UINT next_divisor;

    // figure out initial divisor
    while (number / divisor > 9)
    {
        next_divisor = divisor * 10;
        if (next_divisor > divisor)
            divisor = next_divisor;
        else
            break;
    }

    // now mod and print, then divide divisor
    do
    {
        UNITY_OUTPUT_CHAR((char)('0' + (number / divisor % 10)));
        divisor /= 10;
    }
    while (divisor > 0);
}

//-----------------------------------------------
void UnityPrintNumberHex(const _U_UINT number, const char nibbles_to_print)
{
    _U_UINT nibble;
    char nibbles = nibbles_to_print;
    UNITY_OUTPUT_CHAR('0');
    UNITY_OUTPUT_CHAR('x');

    while (nibbles > 0)
    {
        nibble = (number >> (--nibbles << 2)) & 0x0000000F;
        if (nibble <= 9)
        {
            UNITY_OUTPUT_CHAR((char)('0' + nibble));
        }
        else
        {
            UNITY_OUTPUT_CHAR((char)('A' - 10 + nibble));
        }
    }
}

//-----------------------------------------------
void UnityPrintMask(const _U_UINT mask, const _U_UINT number)
{
    _U_UINT current_bit = (_U_UINT)1 << (UNITY_INT_WIDTH - 1);
    _US32 i;

    for (i = 0; i < UNITY_INT_WIDTH; i++)
    {
        if (current_bit & mask)
        {
            if (current_bit & number)
            {
                UNITY_OUTPUT_CHAR('1');
            }
            else
            {
                UNITY_OUTPUT_CHAR('0');
            }
        }
        else
        {
            UNITY_OUTPUT_CHAR('X');
        }
        current_bit = current_bit >> 1;
    }
}

//-----------------------------------------------
#ifdef UNITY_FLOAT_VERBOSE
#include <string.h>
void UnityPrintFloat(_UF number)
{
    char TempBuffer[32];
    sprintf(TempBuffer, "%.6f", number);
    UnityPrint(TempBuffer);
}
#endif

//-----------------------------------------------

void UnityPrintFail(void)
{
    UnityPrint(UnityStrFail);
}

void UnityPrintOk(void)
{
    UnityPrint(UnityStrOk);
}

//-----------------------------------------------
static void UnityTestResultsBegin(const char* file, const UNITY_LINE_TYPE line)
{
    UnityPrint(file);
    UNITY_OUTPUT_CHAR(':');
    UnityPrintNumber((_U_SINT)line);
    UNITY_OUTPUT_CHAR(':');
    UnityPrint(Unity.CurrentTestName);
    UNITY_OUTPUT_CHAR(':');
}

//-----------------------------------------------
static void UnityTestResultsFailBegin(const UNITY_LINE_TYPE line)
{
    UnityTestResultsBegin(Unity.TestFile, line);
	if (Unity.isExpectingFail)
	{
		UnityPrint(UnityStrXFAIL);
	}
	else
	{
		UnityPrint(UnityStrFail);
	}

    UNITY_OUTPUT_CHAR(':');
}

//-----------------------------------------------
void UnityConcludeTest(void)
{
#if 0
	if (Unity.isExpectingFail == 1 && Unity.CurrentTestFailed == 0)
	{
		printf("FAIL WAS EXPECTED, BUT IT DIDN'T HAPPEN?!");
		Unity.TestXPASSES++;
	}

	else
#endif
	//cant be ignored and accepting fail at the same time!
	if (Unity.isExpectingFail == 1 && Unity.CurrentTestFailed == 1)
	{
		Unity.TestXFAILS++; //error message?!
		if (Unity.XFAILMessage != NULL)
		{
			if (Unity.XFAILMessage[0] != ' ')
			{
				printf(" ");
			}

			printf("| ");
			printf("%s", Unity.XFAILMessage);
			Unity.XFAILMessage = NULL;
		}
		else
		{
			printf(" - EXPECTED FAIL!");
		}
	}
	else

    if (Unity.CurrentTestIgnored)
    {
        Unity.TestIgnores++;
    }
    else if (!Unity.CurrentTestFailed)
    {
	if(Unity.isExpectingFail == 0) {
		UnityTestResultsBegin(Unity.TestFile, Unity.CurrentTestLineNumber);
		UnityPrint(UnityStrPass);
		Unity.TestPasses++;
	}

	//probably should remove the if... part
	else if (Unity.isExpectingFail == 1 && Unity.CurrentTestFailed == 0)
	{

		UnityTestResultsBegin(Unity.TestFile, Unity.CurrentTestLineNumber);
		UnityPrint(UnityStrXPASS);
		Unity.TestXPASSES++;

		printf(" - FAIL WAS EXPECTED, BUT DIDN'T HAPPEN?!");
		//if (Unity.TestPasses > 0) { Unity.TestPasses--; }
	}
    }
    else
    {
        Unity.TestFailures++;
    }

    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
    Unity.isExpectingFail = 0;

    UNITY_PRINT_EOL;
}

//-----------------------------------------------
static void UnityAddMsgIfSpecified(const char* msg)
{
    if (msg)
    {
        UnityPrint(UnityStrSpacer);
        UnityPrint(msg);
    }
}

//-----------------------------------------------
static void UnityPrintExpectedAndActualStrings(const char* expected, const char* actual)
{
    UnityPrint(UnityStrExpected);
    if (expected != NULL)
    {
        UNITY_OUTPUT_CHAR('\'');
        UnityPrint(expected);
        UNITY_OUTPUT_CHAR('\'');
    }
    else
    {
      UnityPrint(UnityStrNull);
    }
    UnityPrint(UnityStrWas);
    if (actual != NULL)
    {
        UNITY_OUTPUT_CHAR('\'');
        UnityPrint(actual);
        UNITY_OUTPUT_CHAR('\'');
    }
    else
    {
      UnityPrint(UnityStrNull);
    }
}

//-----------------------------------------------
// Assertion & Control Helpers
//-----------------------------------------------

static int UnityCheckArraysForNull(UNITY_PTR_ATTRIBUTE const void* expected, UNITY_PTR_ATTRIBUTE const void* actual, const UNITY_LINE_TYPE lineNumber, const char* msg)
{
    //return true if they are both NULL
    if ((expected == NULL) && (actual == NULL))
        return 1;

    //throw error if just expected is NULL
    if (expected == NULL)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrNullPointerForExpected);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }

    //throw error if just actual is NULL
    if (actual == NULL)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrNullPointerForActual);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }

    //return false if neither is NULL
    return 0;
}

//-----------------------------------------------
// Assertion Functions
//-----------------------------------------------

void UnityAssertBits(const _U_SINT mask,
                     const _U_SINT expected,
                     const _U_SINT actual,
                     const char* msg,
                     const UNITY_LINE_TYPE lineNumber)
{
    UNITY_SKIP_EXECUTION;

    if ((mask & expected) != (mask & actual))
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrExpected);
        UnityPrintMask((_U_UINT)mask, (_U_UINT)expected);
        UnityPrint(UnityStrWas);
        UnityPrintMask((_U_UINT)mask, (_U_UINT)actual);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }
}

//-----------------------------------------------
void UnityAssertEqualNumber(const _U_SINT expected,
                            const _U_SINT actual,
                            const char* msg,
                            const UNITY_LINE_TYPE lineNumber,
                            const UNITY_DISPLAY_STYLE_T style)
{
    UNITY_SKIP_EXECUTION;

    if (expected != actual)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrExpected);
        UnityPrintNumberByStyle(expected, style);
        UnityPrint(UnityStrWas);
        UnityPrintNumberByStyle(actual, style);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }
}

//-----------------------------------------------
void UnityAssertEqualIntArray(UNITY_PTR_ATTRIBUTE const void* expected,
                              UNITY_PTR_ATTRIBUTE const void* actual,
                              const _UU32 num_elements,
                              const char* msg,
                              const UNITY_LINE_TYPE lineNumber,
                              const UNITY_DISPLAY_STYLE_T style)
{
    _UU32 elements = num_elements;
    UNITY_PTR_ATTRIBUTE const _US8* ptr_exp = (UNITY_PTR_ATTRIBUTE const _US8*)expected;
    UNITY_PTR_ATTRIBUTE const _US8* ptr_act = (UNITY_PTR_ATTRIBUTE const _US8*)actual;

    UNITY_SKIP_EXECUTION;

    if (elements == 0)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrPointless);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }

    if (UnityCheckArraysForNull((UNITY_PTR_ATTRIBUTE const void*)expected, (UNITY_PTR_ATTRIBUTE const void*)actual, lineNumber, msg) == 1)
        return;

    // If style is UNITY_DISPLAY_STYLE_INT, we'll fall into the default case rather than the INT16 or INT32 (etc) case
    // as UNITY_DISPLAY_STYLE_INT includes a flag for UNITY_DISPLAY_RANGE_AUTO, which the width-specific
    // variants do not. Therefore remove this flag.
    switch(style & (UNITY_DISPLAY_STYLE_T)(~UNITY_DISPLAY_RANGE_AUTO))
    {
        case UNITY_DISPLAY_STYLE_HEX8:
        case UNITY_DISPLAY_STYLE_INT8:
        case UNITY_DISPLAY_STYLE_UINT8:
            while (elements--)
            {
                if (*ptr_exp != *ptr_act)
                {
                    UnityTestResultsFailBegin(lineNumber);
                    UnityPrint(UnityStrElement);
                    UnityPrintNumberByStyle((num_elements - elements - 1), UNITY_DISPLAY_STYLE_UINT);
                    UnityPrint(UnityStrExpected);
                    UnityPrintNumberByStyle(*ptr_exp, style);
                    UnityPrint(UnityStrWas);
                    UnityPrintNumberByStyle(*ptr_act, style);
                    UnityAddMsgIfSpecified(msg);
                    UNITY_FAIL_AND_BAIL;
                }
                ptr_exp += 1;
                ptr_act += 1;
            }
            break;
        case UNITY_DISPLAY_STYLE_HEX16:
        case UNITY_DISPLAY_STYLE_INT16:
        case UNITY_DISPLAY_STYLE_UINT16:
            while (elements--)
            {
                if (*(UNITY_PTR_ATTRIBUTE const _US16*)(void*)ptr_exp != *(UNITY_PTR_ATTRIBUTE const _US16*)(void*)ptr_act)
                {
                    UnityTestResultsFailBegin(lineNumber);
                    UnityPrint(UnityStrElement);
                    UnityPrintNumberByStyle((num_elements - elements - 1), UNITY_DISPLAY_STYLE_UINT);
                    UnityPrint(UnityStrExpected);
                    UnityPrintNumberByStyle(*(UNITY_PTR_ATTRIBUTE const _US16*)(void*)ptr_exp, style);
                    UnityPrint(UnityStrWas);
                    UnityPrintNumberByStyle(*(UNITY_PTR_ATTRIBUTE const _US16*)(void*)ptr_act, style);
                    UnityAddMsgIfSpecified(msg);
                    UNITY_FAIL_AND_BAIL;
                }
                ptr_exp += 2;
                ptr_act += 2;
            }
            break;
#ifdef UNITY_SUPPORT_64
        case UNITY_DISPLAY_STYLE_HEX64:
        case UNITY_DISPLAY_STYLE_INT64:
        case UNITY_DISPLAY_STYLE_UINT64:
            while (elements--)
            {
                if (*(UNITY_PTR_ATTRIBUTE const _US64*)(void*)ptr_exp != *(UNITY_PTR_ATTRIBUTE const _US64*)(void*)ptr_act)
                {
                    UnityTestResultsFailBegin(lineNumber);
                    UnityPrint(UnityStrElement);
                    UnityPrintNumberByStyle((num_elements - elements - 1), UNITY_DISPLAY_STYLE_UINT);
                    UnityPrint(UnityStrExpected);
                    UnityPrintNumberByStyle(*(UNITY_PTR_ATTRIBUTE const _US64*)(void*)ptr_exp, style);
                    UnityPrint(UnityStrWas);
                    UnityPrintNumberByStyle(*(UNITY_PTR_ATTRIBUTE const _US64*)(void*)ptr_act, style);
                    UnityAddMsgIfSpecified(msg);
                    UNITY_FAIL_AND_BAIL;
                }
                ptr_exp += 8;
                ptr_act += 8;
            }
            break;
#endif
        default:
            while (elements--)
            {
                if (*(UNITY_PTR_ATTRIBUTE const _US32*)(void*)ptr_exp != *(UNITY_PTR_ATTRIBUTE const _US32*)(void*)ptr_act)
                {
                    UnityTestResultsFailBegin(lineNumber);
                    UnityPrint(UnityStrElement);
                    UnityPrintNumberByStyle((num_elements - elements - 1), UNITY_DISPLAY_STYLE_UINT);
                    UnityPrint(UnityStrExpected);
                    UnityPrintNumberByStyle(*(UNITY_PTR_ATTRIBUTE const _US32*)(void*)ptr_exp, style);
                    UnityPrint(UnityStrWas);
                    UnityPrintNumberByStyle(*(UNITY_PTR_ATTRIBUTE const _US32*)(void*)ptr_act, style);
                    UnityAddMsgIfSpecified(msg);
                    UNITY_FAIL_AND_BAIL;
                }
                ptr_exp += 4;
                ptr_act += 4;
            }
            break;
    }
}

//-----------------------------------------------
#ifndef UNITY_EXCLUDE_FLOAT
void UnityAssertEqualFloatArray(UNITY_PTR_ATTRIBUTE const _UF* expected,
                                UNITY_PTR_ATTRIBUTE const _UF* actual,
                                const _UU32 num_elements,
                                const char* msg,
                                const UNITY_LINE_TYPE lineNumber)
{
    _UU32 elements = num_elements;
    UNITY_PTR_ATTRIBUTE const _UF* ptr_expected = expected;
    UNITY_PTR_ATTRIBUTE const _UF* ptr_actual = actual;
    _UF diff, tol;

    UNITY_SKIP_EXECUTION;

    if (elements == 0)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrPointless);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }

    if (UnityCheckArraysForNull((UNITY_PTR_ATTRIBUTE const void*)expected, (UNITY_PTR_ATTRIBUTE const void*)actual, lineNumber, msg) == 1)
        return;

    while (elements--)
    {
        diff = *ptr_expected - *ptr_actual;
        if (diff < 0.0f)
          diff = 0.0f - diff;
        tol = UNITY_FLOAT_PRECISION * *ptr_expected;
        if (tol < 0.0f)
            tol = 0.0f - tol;

        //This first part of this condition will catch any NaN or Infinite values
        if ((diff * 0.0f != 0.0f) || (diff > tol))
        {
            UnityTestResultsFailBegin(lineNumber);
            UnityPrint(UnityStrElement);
            UnityPrintNumberByStyle((num_elements - elements - 1), UNITY_DISPLAY_STYLE_UINT);
#ifdef UNITY_FLOAT_VERBOSE
            UnityPrint(UnityStrExpected);
            UnityPrintFloat(*ptr_expected);
            UnityPrint(UnityStrWas);
            UnityPrintFloat(*ptr_actual);
#else
            UnityPrint(UnityStrDelta);
#endif
            UnityAddMsgIfSpecified(msg);
            UNITY_FAIL_AND_BAIL;
        }
        ptr_expected++;
        ptr_actual++;
    }
}

//-----------------------------------------------
void UnityAssertFloatsWithin(const _UF delta,
                             const _UF expected,
                             const _UF actual,
                             const char* msg,
                             const UNITY_LINE_TYPE lineNumber)
{
    _UF diff = actual - expected;
    _UF pos_delta = delta;

    UNITY_SKIP_EXECUTION;

    if (diff < 0.0f)
    {
        diff = 0.0f - diff;
    }
    if (pos_delta < 0.0f)
    {
        pos_delta = 0.0f - pos_delta;
    }

    //This first part of this condition will catch any NaN or Infinite values
    if ((diff * 0.0f != 0.0f) || (pos_delta < diff))
    {
        UnityTestResultsFailBegin(lineNumber);
#ifdef UNITY_FLOAT_VERBOSE
        UnityPrint(UnityStrExpected);
        UnityPrintFloat(expected);
        UnityPrint(UnityStrWas);
        UnityPrintFloat(actual);
#else
        UnityPrint(UnityStrDelta);
#endif
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }
}

//-----------------------------------------------
void UnityAssertFloatSpecial(const _UF actual,
                             const char* msg,
                             const UNITY_LINE_TYPE lineNumber,
                             const UNITY_FLOAT_TRAIT_T style)
{
    const char* trait_names[] = { UnityStrInf, UnityStrNegInf, UnityStrNaN, UnityStrDet };
    _U_SINT should_be_trait   = ((_U_SINT)style & 1);
    _U_SINT is_trait          = !should_be_trait;
    _U_SINT trait_index       = style >> 1;

    UNITY_SKIP_EXECUTION;

    switch(style)
    {
        //To determine Inf / Neg Inf, we compare to an Inf / Neg Inf value we create on the fly
        //We are using a variable to hold the zero value because some compilers complain about dividing by zero otherwise
        case UNITY_FLOAT_IS_INF:
        case UNITY_FLOAT_IS_NOT_INF:
            is_trait = ((1.0f / f_zero) == actual) ? 1 : 0;
            break;
        case UNITY_FLOAT_IS_NEG_INF:
        case UNITY_FLOAT_IS_NOT_NEG_INF:
            is_trait = ((-1.0f / f_zero) == actual) ? 1 : 0;
            break;

        //NaN is the only floating point value that does NOT equal itself. Therefore if Actual == Actual, then it is NOT NaN.
        case UNITY_FLOAT_IS_NAN:
        case UNITY_FLOAT_IS_NOT_NAN:
            is_trait = (actual == actual) ? 0 : 1;
            break;

        //A determinate number is non infinite and not NaN. (therefore the opposite of the two above)
        case UNITY_FLOAT_IS_DET:
        case UNITY_FLOAT_IS_NOT_DET:
            if ( (actual != actual) || ((1.0f / f_zero) == actual) || ((-1.0f / f_zero) == actual) )
                is_trait = 0;
            else
                is_trait = 1;
            break;
	default:
	    ;
    }

    if (is_trait != should_be_trait)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrExpected);
        if (!should_be_trait)
            UnityPrint(UnityStrNot);
        UnityPrint(trait_names[trait_index]);
        UnityPrint(UnityStrWas);
#ifdef UNITY_FLOAT_VERBOSE
        UnityPrintFloat(actual);
#else
        if (should_be_trait)
            UnityPrint(UnityStrNot);
        UnityPrint(trait_names[trait_index]);
#endif
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }
}

#endif //not UNITY_EXCLUDE_FLOAT

//-----------------------------------------------
#ifndef UNITY_EXCLUDE_DOUBLE
void UnityAssertEqualDoubleArray(UNITY_PTR_ATTRIBUTE const _UD* expected,
                                 UNITY_PTR_ATTRIBUTE const _UD* actual,
                                 const _UU32 num_elements,
                                 const char* msg,
                                 const UNITY_LINE_TYPE lineNumber)
{
    _UU32 elements = num_elements;
    UNITY_PTR_ATTRIBUTE const _UD* ptr_expected = expected;
    UNITY_PTR_ATTRIBUTE const _UD* ptr_actual = actual;
    _UD diff, tol;

    UNITY_SKIP_EXECUTION;

    if (elements == 0)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrPointless);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }

    if (UnityCheckArraysForNull((UNITY_PTR_ATTRIBUTE void*)expected, (UNITY_PTR_ATTRIBUTE void*)actual, lineNumber, msg) == 1)
        return;

    while (elements--)
    {
        diff = *ptr_expected - *ptr_actual;
        if (diff < 0.0)
          diff = 0.0 - diff;
        tol = UNITY_DOUBLE_PRECISION * *ptr_expected;
        if (tol < 0.0)
            tol = 0.0 - tol;

        //This first part of this condition will catch any NaN or Infinite values
        if ((diff * 0.0 != 0.0) || (diff > tol))
        {
            UnityTestResultsFailBegin(lineNumber);
            UnityPrint(UnityStrElement);
            UnityPrintNumberByStyle((num_elements - elements - 1), UNITY_DISPLAY_STYLE_UINT);
#ifdef UNITY_DOUBLE_VERBOSE
            UnityPrint(UnityStrExpected);
            UnityPrintFloat((float)(*ptr_expected));
            UnityPrint(UnityStrWas);
            UnityPrintFloat((float)(*ptr_actual));
#else
            UnityPrint(UnityStrDelta);
#endif
            UnityAddMsgIfSpecified(msg);
            UNITY_FAIL_AND_BAIL;
        }
        ptr_expected++;
        ptr_actual++;
    }
}

//-----------------------------------------------
void UnityAssertDoublesWithin(const _UD delta,
                              const _UD expected,
                              const _UD actual,
                              const char* msg,
                              const UNITY_LINE_TYPE lineNumber)
{
    _UD diff = actual - expected;
    _UD pos_delta = delta;

    UNITY_SKIP_EXECUTION;

    if (diff < 0.0)
    {
        diff = 0.0 - diff;
    }
    if (pos_delta < 0.0)
    {
        pos_delta = 0.0 - pos_delta;
    }

    //This first part of this condition will catch any NaN or Infinite values
    if ((diff * 0.0 != 0.0) || (pos_delta < diff))
    {
        UnityTestResultsFailBegin(lineNumber);
#ifdef UNITY_DOUBLE_VERBOSE
        UnityPrint(UnityStrExpected);
        UnityPrintFloat((float)expected);
        UnityPrint(UnityStrWas);
        UnityPrintFloat((float)actual);
#else
        UnityPrint(UnityStrDelta);
#endif
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }
}

//-----------------------------------------------

void UnityAssertDoubleSpecial(const _UD actual,
                              const char* msg,
                              const UNITY_LINE_TYPE lineNumber,
                              const UNITY_FLOAT_TRAIT_T style)
{
    const char* trait_names[] = { UnityStrInf, UnityStrNegInf, UnityStrNaN, UnityStrDet };
    _U_SINT should_be_trait   = ((_U_SINT)style & 1);
    _U_SINT is_trait          = !should_be_trait;
    _U_SINT trait_index       = style >> 1;

    UNITY_SKIP_EXECUTION;

    switch(style)
    {
        //To determine Inf / Neg Inf, we compare to an Inf / Neg Inf value we create on the fly
        //We are using a variable to hold the zero value because some compilers complain about dividing by zero otherwise
        case UNITY_FLOAT_IS_INF:
        case UNITY_FLOAT_IS_NOT_INF:
            is_trait = ((1.0 / d_zero) == actual) ? 1 : 0;
            break;
        case UNITY_FLOAT_IS_NEG_INF:
        case UNITY_FLOAT_IS_NOT_NEG_INF:
            is_trait = ((-1.0 / d_zero) == actual) ? 1 : 0;
            break;

        //NaN is the only floating point value that does NOT equal itself. Therefore if Actual == Actual, then it is NOT NaN.
        case UNITY_FLOAT_IS_NAN:
        case UNITY_FLOAT_IS_NOT_NAN:
            is_trait = (actual == actual) ? 0 : 1;
            break;

        //A determinate number is non infinite and not NaN. (therefore the opposite of the two above)
        case UNITY_FLOAT_IS_DET:
        case UNITY_FLOAT_IS_NOT_DET:
            if ( (actual != actual) || ((1.0 / d_zero) == actual) || ((-1.0 / d_zero) == actual) )
                is_trait = 0;
            else
                is_trait = 1;
            break;
	default:
	    ;
    }

    if (is_trait != should_be_trait)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrExpected);
        if (!should_be_trait)
            UnityPrint(UnityStrNot);
        UnityPrint(trait_names[trait_index]);
        UnityPrint(UnityStrWas);
#ifdef UNITY_DOUBLE_VERBOSE
        UnityPrintFloat(actual);
#else
        if (should_be_trait)
            UnityPrint(UnityStrNot);
        UnityPrint(trait_names[trait_index]);
#endif
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }
}


#endif // not UNITY_EXCLUDE_DOUBLE

//-----------------------------------------------
void UnityAssertNumbersWithin( const _U_SINT delta,
                               const _U_SINT expected,
                               const _U_SINT actual,
                               const char* msg,
                               const UNITY_LINE_TYPE lineNumber,
                               const UNITY_DISPLAY_STYLE_T style)
{
    UNITY_SKIP_EXECUTION;

    if ((style & UNITY_DISPLAY_RANGE_INT) == UNITY_DISPLAY_RANGE_INT)
    {
        if (actual > expected)
          Unity.CurrentTestFailed = ((actual - expected) > delta);
        else
          Unity.CurrentTestFailed = ((expected - actual) > delta);
    }
    else
    {
        if ((_U_UINT)actual > (_U_UINT)expected)
            Unity.CurrentTestFailed = ((_U_UINT)(actual - expected) > (_U_UINT)delta);
        else
            Unity.CurrentTestFailed = ((_U_UINT)(expected - actual) > (_U_UINT)delta);
    }

    if (Unity.CurrentTestFailed)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrDelta);
        UnityPrintNumberByStyle(delta, style);
        UnityPrint(UnityStrExpected);
        UnityPrintNumberByStyle(expected, style);
        UnityPrint(UnityStrWas);
        UnityPrintNumberByStyle(actual, style);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }
}

//-----------------------------------------------
void UnityAssertEqualString(const char* expected,
                            const char* actual,
                            const char* msg,
                            const UNITY_LINE_TYPE lineNumber)
{
    _UU32 i;

    UNITY_SKIP_EXECUTION;

    // if both pointers not null compare the strings
    if (expected && actual)
    {
        for (i = 0; expected[i] || actual[i]; i++)
        {
            if (expected[i] != actual[i])
            {
                Unity.CurrentTestFailed = 1;
                break;
            }
        }
    }
    else
    { // handle case of one pointers being null (if both null, test should pass)
        if (expected != actual)
        {
            Unity.CurrentTestFailed = 1;
        }
    }

    if (Unity.CurrentTestFailed)
    {
      UnityTestResultsFailBegin(lineNumber);
      UnityPrintExpectedAndActualStrings(expected, actual);
      UnityAddMsgIfSpecified(msg);
      UNITY_FAIL_AND_BAIL;
    }
}

//-----------------------------------------------
void UnityAssertEqualStringArray( const char** expected,
                                  const char** actual,
                                  const _UU32 num_elements,
                                  const char* msg,
                                  const UNITY_LINE_TYPE lineNumber)
{
    _UU32 i, j = 0;

    UNITY_SKIP_EXECUTION;

    // if no elements, it's an error
    if (num_elements == 0)
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrPointless);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }

    if (UnityCheckArraysForNull((UNITY_PTR_ATTRIBUTE void*)expected, (UNITY_PTR_ATTRIBUTE void*)actual, lineNumber, msg) == 1)
        return;

    do
    {
        // if both pointers not null compare the strings
        if (expected[j] && actual[j])
        {
            for (i = 0; expected[j][i] || actual[j][i]; i++)
            {
                if (expected[j][i] != actual[j][i])
                {
                    Unity.CurrentTestFailed = 1;
                    break;
                }
            }
        }
        else
        { // handle case of one pointers being null (if both null, test should pass)
            if (expected[j] != actual[j])
            {
                Unity.CurrentTestFailed = 1;
            }
        }

        if (Unity.CurrentTestFailed)
        {
            UnityTestResultsFailBegin(lineNumber);
            if (num_elements > 1)
            {
                UnityPrint(UnityStrElement);
                UnityPrintNumberByStyle((j), UNITY_DISPLAY_STYLE_UINT);
            }
            UnityPrintExpectedAndActualStrings((const char*)(expected[j]), (const char*)(actual[j]));
            UnityAddMsgIfSpecified(msg);
            UNITY_FAIL_AND_BAIL;
        }
    } while (++j < num_elements);
}

//-----------------------------------------------
void UnityAssertEqualMemory( UNITY_PTR_ATTRIBUTE const void* expected,
                             UNITY_PTR_ATTRIBUTE const void* actual,
                             const _UU32 length,
                             const _UU32 num_elements,
                             const char* msg,
                             const UNITY_LINE_TYPE lineNumber)
{
    UNITY_PTR_ATTRIBUTE const unsigned char* ptr_exp = (UNITY_PTR_ATTRIBUTE const unsigned char*)expected;
    UNITY_PTR_ATTRIBUTE const unsigned char* ptr_act = (UNITY_PTR_ATTRIBUTE const unsigned char*)actual;
    _UU32 elements = num_elements;
    _UU32 bytes;

    UNITY_SKIP_EXECUTION;

    if ((elements == 0) || (length == 0))
    {
        UnityTestResultsFailBegin(lineNumber);
        UnityPrint(UnityStrPointless);
        UnityAddMsgIfSpecified(msg);
        UNITY_FAIL_AND_BAIL;
    }

    if (UnityCheckArraysForNull((UNITY_PTR_ATTRIBUTE const void*)expected, (UNITY_PTR_ATTRIBUTE const void*)actual, lineNumber, msg) == 1)
        return;

    while (elements--)
    {
        /////////////////////////////////////
        bytes = length;
        while (bytes--)
        {
            if (*ptr_exp != *ptr_act)
            {
                UnityTestResultsFailBegin(lineNumber);
                UnityPrint(UnityStrMemory);
                if (num_elements > 1)
                {
                    UnityPrint(UnityStrElement);
                    UnityPrintNumberByStyle((num_elements - elements - 1), UNITY_DISPLAY_STYLE_UINT);
                }
                UnityPrint(UnityStrByte);
                UnityPrintNumberByStyle((length - bytes - 1), UNITY_DISPLAY_STYLE_UINT);
                UnityPrint(UnityStrExpected);
                UnityPrintNumberByStyle(*ptr_exp, UNITY_DISPLAY_STYLE_HEX8);
                UnityPrint(UnityStrWas);
                UnityPrintNumberByStyle(*ptr_act, UNITY_DISPLAY_STYLE_HEX8);
                UnityAddMsgIfSpecified(msg);
                UNITY_FAIL_AND_BAIL;
            }
            ptr_exp += 1;
            ptr_act += 1;
        }
        /////////////////////////////////////

    }
}

//-----------------------------------------------
// Control Functions
//-----------------------------------------------

void UnityFail(const char* msg, const UNITY_LINE_TYPE line)
{
    UNITY_SKIP_EXECUTION;

    UnityTestResultsBegin(Unity.TestFile, line);
    UnityPrintFail();
    if (msg != NULL)
    {
      UNITY_OUTPUT_CHAR(':');
      if (msg[0] != ' ')
      {
        UNITY_OUTPUT_CHAR(' ');
      }
      UnityPrint(msg);
    }
    UNITY_FAIL_AND_BAIL;
}

//-----------------------------------------------
void UnityIgnore(const char* msg, const UNITY_LINE_TYPE line)
{
    UNITY_SKIP_EXECUTION;

    UnityTestResultsBegin(Unity.TestFile, line);
    UnityPrint(UnityStrIgnore);
    if (msg != NULL)
    {
      UNITY_OUTPUT_CHAR(':');
      UNITY_OUTPUT_CHAR(' ');
      UnityPrint(msg);
    }
    UNITY_IGNORE_AND_BAIL;
}

//----------------------------------------------

void UnityExpectFail(){

	Unity.isExpectingFail = 1;

}

void UnityExpectFailMessage(const char* msg, const UNITY_LINE_TYPE line ){

	Unity.isExpectingFail = 1;
	  if (msg != NULL)
    {
		Unity.XFAILMessage = msg;
    }
}

//-----------------------------------------------
#if defined(UNITY_WEAK_ATTRIBUTE)
    void setUp(void);
    void tearDown(void);
    UNITY_WEAK_ATTRIBUTE void setUp(void) { }
    UNITY_WEAK_ATTRIBUTE void tearDown(void) { }
#elif defined(UNITY_WEAK_PRAGMA)
#   pragma weak setUp
    void setUp(void);
#   pragma weak tearDown
    void tearDown(void);
#else
    void setUp(void);
    void tearDown(void);
#endif

//-----------------------------------------------
void UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, const int FuncLineNum)
{
    Unity.CurrentTestName = FuncName;
    Unity.CurrentTestLineNumber = (UNITY_LINE_TYPE)FuncLineNum;
    Unity.NumberOfTests++;

    if (TEST_PROTECT())
    {
        setUp();
        Func();
    }
    if (TEST_PROTECT() && !(Unity.CurrentTestIgnored))
    {
        tearDown();
    }

    UnityConcludeTest();
}


//-----------------------------------------------
void UnityBegin(const char* filename)
{
    Unity.TestFile = filename;
    Unity.CurrentTestName = NULL;
    Unity.CurrentTestLineNumber = 0;
    Unity.NumberOfTests = 0;
    Unity.TestFailures = 0;
    Unity.TestIgnores = 0;
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
    Unity.TestXFAILS = 0;
    Unity.isExpectingFail = 0;
    Unity.TestPasses = 0;
    Unity.TestXPASSES = 0;
    Unity.XFAILMessage = NULL;

    UNITY_OUTPUT_START();
}


//-----------------------------------------------
int UnityEnd(void)
{
    UNITY_PRINT_EOL;
    UnityPrint(UnityStrBreaker);
    UNITY_PRINT_EOL;
    UnityPrintNumber((_U_SINT)(Unity.NumberOfTests));
    UnityPrint(UnityStrResultsTests);
    UNITY_PRINT_EOL;
    UnityPrintNumber((_U_SINT)(Unity.TestPasses));
    UnityPrint(UnityStrResultsPass);
    UNITY_PRINT_EOL;
    UnityPrintNumber((_U_SINT)(Unity.TestXFAILS));
    UnityPrint(UnityStrResultsXFAIL);
    UNITY_PRINT_EOL;
    UnityPrintNumber((_U_SINT)(Unity.TestFailures));
    UnityPrint(UnityStrResultsFailures);
    UNITY_PRINT_EOL;
    UnityPrintNumber((_U_SINT)(Unity.TestXPASSES));
    UnityPrint(UnityStrResultsXPASS);
    UNITY_PRINT_EOL;
    UnityPrintNumber((_U_SINT)(Unity.TestIgnores));
    UnityPrint(UnityStrResultsIgnored);
    UNITY_PRINT_EOL;

    UNITY_PRINT_EOL;
    if (Unity.TestFailures == 0U && Unity.TestXPASSES == 0U)
    {
        UnityPrintOk();
    }
    else
    {
        UnityPrintFail();
    }
    UNITY_PRINT_EOL;
    UNITY_OUTPUT_COMPLETE();
    return (int)(Unity.TestFailures);
}


//-----------------------------------------------
