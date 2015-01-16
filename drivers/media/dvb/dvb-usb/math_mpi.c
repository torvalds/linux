/**

@file

@brief   Mutliple precision integer (MPI) arithmetic definition

One can use to mutliple precision arithmetic to manipulate large signed integers.

*/


#include "math_mpi.h"





/**

@brief   Set multiple precision signed integer value.

Use MpiSetValue() to set multiple precision signed integer MPI value.


@param [in]   pMpiVar   Pointer to an MPI variable
@param [in]   Value     Value for setting


@note
	The MPI bit number will be minimized in MpiSetValue().

*/
void
MpiSetValue(
	MPI *pMpiVar,
	long Value
	)
{
	int i;
	unsigned char SignedBit;
	unsigned char ExtensionByte;



	// Set MPI value according to ansigned value.
	for(i = 0; i < MPI_LONG_BYTE_NUM; i++)
		pMpiVar->Value[i] = (unsigned char)((Value >> (MPI_BYTE_SHIFT * i)) & MPI_BYTE_MASK);

	
	// Get extension byte according to signed bit.
	SignedBit = (unsigned char)((Value >> (MPI_LONG_BIT_NUM - 1)) & MPI_BIT_0_MASK);
	ExtensionByte = (SignedBit == 0x0) ? 0x00 : 0xff;


	// Extend MPI signed bit with extension byte stuff.
	for(i = MPI_LONG_BYTE_NUM; i < MPI_VALUE_BYTE_NUM_MAX; i++)
		pMpiVar->Value[i] = ExtensionByte;


	// Minimize MPI bit number.
	MpiMinimizeBitNum(pMpiVar);


	return;
}





/**

@brief   Get multiple precision signed integer value.

Use MpiGetValue() to get multiple precision unsigned integer MPI value.


@param [in]    MpiVar   Pointer to an MPI variable
@param [out]   pValue   Pointer to an allocated memory for getting MPI value


@note
    The necessary bit number of MPI value must be less than or equal to 32 bits.

*/
void
MpiGetValue(
	MPI MpiVar,
	long *pValue
	)
{
	int i;
	unsigned long Value;



	// Set value with zero.
	Value = 0x0;


	// Combine MPI value bytes into value.
	for(i = 0; i < MPI_LONG_BYTE_NUM; i++)
		Value |= MpiVar.Value[i] << (MPI_BYTE_SHIFT * i);


	// Assigned value to value pointer.
	*pValue = (long)Value;


	return;
}





/**

@brief   Set multiple precision signed integer bit value.

Use MpiSetBit() to set multiple precision signed integer MPI bit value.


@param [in]   pMpiVar       Pointer to an MPI variable
@param [in]   BitPosition   Bit position with zero-based index
@param [in]   BitValue      Bit value for setting


@note
	Bit position must be 0 ~ (MPI bit number).

*/
void
MpiSetBit(
	MPI *pMpiVar,
	unsigned long BitPosition,
	unsigned char BitValue
	)
{
	unsigned long TargetBytePos, TargetBitPos;



	// Calculate target byte and bit position.
	TargetBytePos = BitPosition / MPI_BYTE_BIT_NUM;
	TargetBitPos  = BitPosition % MPI_BYTE_BIT_NUM;


	// Set MPI bit value according to calculated target byte and bit position.
	pMpiVar->Value[TargetBytePos] &= (unsigned char)(~(0x1 << TargetBitPos));
	pMpiVar->Value[TargetBytePos] |= (BitValue & MPI_BIT_0_MASK) << TargetBitPos;


	return;
}






/**

@brief   Get multiple precision signed integer bit value.

Use MpiGetBit() to get multiple precision unsigned integer MPI bit value.


@param [in]    MpiVar        Pointer to an MPI variable
@param [in]    BitPosition   Bit position with zero-based index
@param [out]   pBitValue     Pointer to an allocated memory for getting MPI bit value


@note
	Bit position must be 0 ~ (MPI bit number).

*/
void
MpiGetBit(
	MPI MpiVar,
	unsigned long BitPosition,
	unsigned char *pBitValue
	)
{
	unsigned long TargetBytePos, TargetBitPos;



	// Calculate target byte and bit position.
	TargetBytePos = BitPosition / MPI_BYTE_BIT_NUM;
	TargetBitPos  = BitPosition % MPI_BYTE_BIT_NUM;


	// Get MPI bit value according to calculated target byte and bit position.
	*pBitValue = (MpiVar.Value[TargetBytePos] >> TargetBitPos) & MPI_BIT_0_MASK;


	return;
}
	



	
/**

@brief   Get multiple precision signed integer signed bit value.

Use MpiGetBit() to get multiple precision unsigned integer MPI signed bit value.


@param [in]    MpiVar            Pointer to an MPI variable
@param [out]   pSignedBitValue   Pointer to an allocated memory for getting MPI signed bit value

*/
void
MpiGetSignedBit(
	MPI MpiVar,
	unsigned char *pSignedBitValue
	)
{
	// Get MPI variable signed bit.
	MpiGetBit(MpiVar, MPI_VALUE_BIT_NUM_MAX - 1, pSignedBitValue);


	return;
}





/**

@brief   Assign multiple precision signed integer with another one.

Use MpiAssign() to assign multiple precision signed integer with another one.


@param [out]   pResult   Pointer to an allocated memory for storing result
@param [in]    Operand   Operand


@note
	The result bit number will be minimized in MpiAssign().

*/
void
MpiAssign(
	MPI *pResult,
	MPI Operand
	)
{
	unsigned int i;



	// Copy value bytes from operand to result.
	for(i = 0; i < MPI_VALUE_BYTE_NUM_MAX; i++)
		pResult->Value[i] = Operand.Value[i];


	// Minimize result bit nubmer.
	MpiMinimizeBitNum(pResult);


	return;
}





/**

@brief   Minus unary multiple precision signed integer.

Use MpiUnaryMinus() to minus unary multiple precision signed integer.


@param [out]   pResult   Pointer to an allocated memory for storing result
@param [in]    Operand   Operand


@note
	The result bit number will be minimized in MpiUnaryMinus().

*/
void
MpiUnaryMinus(
	MPI *pResult,
	MPI Operand
	)
{
	unsigned int i;
	MPI Const;



	// Set result value byte with operand bitwise complement value byte.
	for(i = 0; i < MPI_VALUE_BYTE_NUM_MAX; i++)
		pResult->Value[i] = ~Operand.Value[i];


	// Add result with 0x1.
	// Note: MpiAdd() will minimize result bit number.
	MpiSetValue(&Const, 0x1);
	MpiAdd(pResult, *pResult, Const);


	return;
}





/**

@brief   Add multiple precision signed integers.

Use MpiAdd() to add multiple precision signed integers.


@param [out]   pSum     Pointer to an allocated memory for storing sum
@param [in]    Augend   Augend
@param [in]    Addend   Addend


@note
	The sum bit number will be minimized in MpiAdd().

*/
void
MpiAdd(
	MPI *pSum,
	MPI Augend,
	MPI Addend
	)
{
	unsigned int i;
	unsigned long MiddleResult;
	unsigned char Carry;


	// Add augend and addend to sum form value LSB byte to value MSB byte.
	Carry = 0;

	for(i = 0; i < MPI_VALUE_BYTE_NUM_MAX; i++)
	{
		// Set current sum value byte and determine carry.
		MiddleResult   = Augend.Value[i] + Addend.Value[i] + Carry;
		pSum->Value[i] = (unsigned char)(MiddleResult & MPI_BYTE_MASK);
		Carry          = (unsigned char)((MiddleResult >> MPI_BYTE_SHIFT) & MPI_BYTE_MASK);
	}
	

	// Minimize sum bit nubmer.
	MpiMinimizeBitNum(pSum);


	return;
}





/**

@brief   subtract multiple precision signed integers.

Use MpiSub() to subtract multiple precision signed integers.


@param [out]   pDifference   Pointer to an allocated memory for storing difference
@param [in]    Minuend       Minuend
@param [in]    Subtrahend    Subtrahend


@note
	The difference bit number will be minimized in MpiSub().

*/
void
MpiSub(
	MPI *pDifference,
	MPI Minuend,
	MPI Subtrahend
	)
{
	MPI MiddleResult;



	// Take subtrahend unary minus value.
	MpiUnaryMinus(&MiddleResult, Subtrahend);


	// Add minuend and subtrahend unary minus value to difference.
	// Note: MpiAdd() will minimize result bit number.
	MpiAdd(pDifference, Minuend, MiddleResult);


	return;
}





/**

@brief   Multiply arbitrary precision signed integers.

Use MpiMul() to multiply arbitrary precision signed integers.


@param [out]   pProduct        Pointer to an allocated memory for storing product
@param [in]    Multiplicand    Multiplicand
@param [in]    Multiplicator   Multiplicator


@note
	-# The sum of multiplicand and multiplicator bit number must be less MPI_VALUE_BIT_NUM_MAX.
	-# The product bit number will be minimized in MpiMul().

*/
void
MpiMul(
	MPI *pProduct,
	MPI Multiplicand,
	MPI Multiplicator
	)
{
	int i;

	unsigned char MultiplicandSignedBit, MultiplicatorSignedBit;
	MPI MultiplicandAbs, MultiplicatorAbs;

	unsigned char CurrentBit;



	// Get multiplicand signed bit.
	MpiGetSignedBit(Multiplicand, &MultiplicandSignedBit);

	// Take absolute value of multiplicand.
	if(MultiplicandSignedBit == 0x0)
		MpiAssign(&MultiplicandAbs, Multiplicand);
	else
		MpiUnaryMinus(&MultiplicandAbs, Multiplicand);


	// Get multiplicator signed bit.
	MpiGetSignedBit(Multiplicator, &MultiplicatorSignedBit);

	// Take absolute value of multiplicator.
	if(MultiplicatorSignedBit == 0x0)
		MpiAssign(&MultiplicatorAbs, Multiplicator);
	else
		MpiUnaryMinus(&MultiplicatorAbs, Multiplicator);


	// Multiply multiplicand and multiplicator from LSB bit to MSB bit.
	MpiSetValue(pProduct, 0x0);

	for(i = MPI_VALUE_BIT_NUM_MAX - 1; i > -1; i--)
	{
		// Shift product toward left with one bit.
		MpiLeftShift(pProduct, *pProduct, 1);

		// Get current absolute multiplicator bit value.
		MpiGetBit(MultiplicatorAbs, i, &CurrentBit);

		// If current multiplicator bit is 0x1, add absolute multiplicand value to product.
		// Note: MpiAdd() will minimize result bit number.
		if(CurrentBit == 0x1)
			MpiAdd(pProduct, *pProduct, MultiplicandAbs);
	}


	// Determine the signed bit of product according to signed bits of multiplicand and multiplicator.
	// Note: MpiUnaryMinus() will minimize result bit number.
	if(MultiplicandSignedBit != MultiplicatorSignedBit)
		MpiUnaryMinus(pProduct, *pProduct);


	return;
}





/**

@brief   Divide arbitrary precision signed integers.

Use MpiDiv() to divide arbitrary precision signed integers.


@param [out]   pQuotient    Pointer to an allocated memory for storing quotient
@param [out]   pRemainder   Pointer to an allocated memory for storing remainder
@param [in]    Dividend     Dividend
@param [in]    Divisor      Divisor


@note
	-# The dividend bit number must be minimized.
	-# The divisor must be not equal to zero.
	-# The product bit number will be minimized in MpiDiv().

*/
void
MpiDiv(
	MPI *pQuotient,
	MPI *pRemainder,
	MPI Dividend,
	MPI Divisor
	)
{
	unsigned int i;

	unsigned char DividendSignedBit, DivisorSignedBit;
	MPI DividendAbs, DivisorAbs;

	unsigned long PrimaryDividendBitNum;
	unsigned char ShiftBit;

	MPI Const;
	MPI MiddleResult;



	// Get dividend signed bit.
	MpiGetSignedBit(Dividend, &DividendSignedBit);

	// Take absolute value of dividend.
	if(DividendSignedBit == 0x0)
		MpiAssign(&DividendAbs, Dividend);
	else
		MpiUnaryMinus(&DividendAbs, Dividend);


	// Get divisor signed bit.
	MpiGetSignedBit(Divisor, &DivisorSignedBit);

	// Take absolute value of divisor.
	if(DivisorSignedBit == 0x0)
		MpiAssign(&DivisorAbs, Divisor);
	else
		MpiUnaryMinus(&DivisorAbs, Divisor);


	// Get primary absolute dividend bit number.
	PrimaryDividendBitNum = DividendAbs.BitNum;


	// Get quotient and remainder by division algorithm.
	MpiSetValue(pQuotient, 0x0);
	MpiSetValue(pRemainder, 0x0);

	for(i = 0; i < PrimaryDividendBitNum; i++)
	{
		// Shift quotient toward left with one bit.
		// Note: MpiLeftShift() will minimize result bit number.
		MpiLeftShift(pQuotient, *pQuotient, 1);

		// Shift remainder toward left with one bit.
		MpiLeftShift(pRemainder, *pRemainder, 1);

		// Shift absolute dividend toward left with one bit.
		MpiLeftShift(&DividendAbs, DividendAbs, 1);

		// Set remainder LSB according to absolute dividend.
		MpiGetBit(DividendAbs, PrimaryDividendBitNum, &ShiftBit);
		MpiSetBit(pRemainder, 0, ShiftBit);

		// If remainder is greater than or equal to absolute divisor,
		// substract absolute divisor from remainder and set quotient LSB with one.
		if(MpiGreaterThan(*pRemainder, DivisorAbs) || MpiEqualTo(*pRemainder, DivisorAbs))
		{
			MpiSub(pRemainder, *pRemainder, DivisorAbs);
			MpiSetBit(pQuotient, 0, 0x1);
		}
	}


	// Modify quotient according to dividend signed bit, divisor signed bit, and remainder.

	// Determine the signed bit of quotient.
	if(DividendSignedBit != DivisorSignedBit)
	{
		// Take unary minus quotient.
		// Note: MpiUnaryMinus() will minimize result bit number.
		MpiUnaryMinus(pQuotient, *pQuotient);

		// If remainder is greater than zero, subtract 1 from quotient.
		// Note: MpiSub() will minimize result bit number.
		MpiSetValue(&Const, 0x0);

		if(MpiGreaterThan(*pRemainder, Const))
		{
			MpiSetValue(&Const, 0x1);
			MpiSub(pQuotient, *pQuotient, Const);
		}
	}


	// Modify remainder according to dividend, divisor, and quotient.

	// Remainder = dividend - divisor * quotient;
	// Note: MpiSub() will minimize result bit number.
	MpiMul(&MiddleResult, Divisor, *pQuotient);
	MpiSub(pRemainder, Dividend, MiddleResult);


	return;
}





/**

@brief   Shift multiple precision signed integer toward right.

Use MpiRightShift() to shift arbitrary precision signed integer toward right with assigned bit number.


@param [out]   pResult       Pointer to an allocated memory for storing result
@param [in]    Operand       Operand
@param [in]    ShiftBitNum   Shift bit number


@note
	-# The result MSB bits will be stuffed with signed bit
	-# The result bit number will be minimized in MpiRightShift().

*/
void
MpiRightShift(
	MPI *pResult,
	MPI Operand,
	unsigned long ShiftBitNum
	)
{
	unsigned int i;
	unsigned long StuffBitNum;
	unsigned char CurrentBit;
	unsigned char SignedBit;



	// Determine stuff bit number according to shift bit nubmer.
	StuffBitNum = (ShiftBitNum < MPI_VALUE_BIT_NUM_MAX) ? ShiftBitNum : MPI_VALUE_BIT_NUM_MAX;


	// Copy operand bits to result with stuff bit number.
	for(i = 0; i < (MPI_VALUE_BIT_NUM_MAX - StuffBitNum); i++)
	{
		MpiGetBit(Operand, i + StuffBitNum, &CurrentBit);
		MpiSetBit(pResult, i, CurrentBit);
	}


	// Get operand signed bit.
	MpiGetSignedBit(Operand, &SignedBit);


	// Stuff result MSB bits with signed bit.
	for(i = (MPI_VALUE_BIT_NUM_MAX - StuffBitNum); i < MPI_VALUE_BIT_NUM_MAX; i++)
		MpiSetBit(pResult, i, SignedBit);


	// Minimize result bit number.
	MpiMinimizeBitNum(pResult);


	return;
}





/**

@brief   Shift multiple precision signed integer toward left.

Use MpiLeftShift() to shift arbitrary precision signed integer toward left with assigned bit number.


@param [out]   pResult       Pointer to an allocated memory for storing result
@param [in]    Operand       Operand
@param [in]    ShiftBitNum   Shift bit number


@note
	The result bit number will be minimized in MpiLeftShift().

*/
void
MpiLeftShift(
	MPI *pResult,
	MPI Operand,
	unsigned long ShiftBitNum
	)
{
	unsigned int i;
	unsigned long StuffBitNum;
	unsigned char CurrentBit;


	// Determine stuff bit number according to shift bit nubmer.
	StuffBitNum = (ShiftBitNum < MPI_VALUE_BIT_NUM_MAX) ? ShiftBitNum : MPI_VALUE_BIT_NUM_MAX;


	// Stuff result LSB bits with zeros
	for(i = 0; i < StuffBitNum; i++)
		MpiSetBit(pResult, i, 0x0);


	// Copy operand bits to result with stuff bit number.
	for(i = StuffBitNum; i < MPI_VALUE_BIT_NUM_MAX; i++)
	{
		MpiGetBit(Operand, i - StuffBitNum, &CurrentBit);
		MpiSetBit(pResult, i, CurrentBit);
	}


	// Minimize result bit number.
	MpiMinimizeBitNum(pResult);


	return;
}





/**

@brief   Compare multiple precision signed integes with equal-to criterion.

Use MpiEqualTo() to compare multiple precision signed integes with equal-to criterion.


@param [in]   MpiLeft    Left MPI
@param [in]   MpiRight   Right MPI


@retval   MPI_NO    "Left MPI == Right MPI" is false.
@retval   MPI_YES   "Left MPI == Right MPI" is true.


@note
	The constants MPI_YES and MPI_NO are defined in MPI_YES_NO_STATUS enumeration.

*/
int
MpiEqualTo(
	MPI MpiLeft,
	MPI MpiRight
	)
{
	unsigned int i;



	// Check not-equal-to condition.
	for(i = 0; i < MPI_VALUE_BYTE_NUM_MAX; i++)
	{
		if(MpiLeft.Value[i] != MpiRight.Value[i])
			goto condition_others;
	}
		

	// Right MPI is greater than left MPI.
	return MPI_YES;


condition_others:


	// Other conditions.
	return MPI_NO;
}





/**

@brief   Compare multiple precision signed integes with greater-than criterion.

Use MpiGreaterThan() to compare multiple precision signed integes with greater-than criterion.


@param [in]   MpiLeft    Left MPI
@param [in]   MpiRight   Right MPI


@retval   MPI_NO    "Left MPI > Right MPI" is false.
@retval   MPI_YES   "Left MPI > Right MPI" is true.


@note
	The constants MPI_YES and MPI_NO are defined in MPI_YES_NO_STATUS enumeration.

*/
int
MpiGreaterThan(
	MPI MpiLeft,
	MPI MpiRight
	)
{
	MPI MiddleResult;
	unsigned char SignedBit;



	// Check equal-to condition.
	if(MpiEqualTo(MpiLeft, MpiRight) == MPI_YES)
		goto condition_others;


	// Subtract right MPI form left MPI.
	MpiSub(&MiddleResult, MpiLeft, MpiRight);


	// Check less-than condition.
	MpiGetSignedBit(MiddleResult, &SignedBit);

	if(SignedBit == 0x1)
		goto condition_others;


	// Right MPI is greater than left MPI.
	return MPI_YES;


condition_others:


	// Other conditions.
	return MPI_NO;
}





/**

@brief   Compare multiple precision signed integes with less-than criterion.

Use MpiLessThan() to compare multiple precision signed integes with less-than criterion.


@param [in]   MpiLeft    Left MPI
@param [in]   MpiRight   Right MPI


@retval   MPI_NO    "Left MPI < Right MPI" is false.
@retval   MPI_YES   "Left MPI < Right MPI" is true.


@note
	The constants MPI_YES and MPI_NO are defined in MPI_YES_NO_STATUS enumeration.

*/
int
MpiLessThan(
	MPI MpiLeft,
	MPI MpiRight
	)
{
	MPI MiddleResult;
	unsigned char SignedBit;



	// Check equal-to condition.
	if(MpiEqualTo(MpiLeft, MpiRight) == MPI_YES)
		goto condition_others;


	// Subtract right MPI form left MPI.
	MpiSub(&MiddleResult, MpiLeft, MpiRight);


	// Check greater-than condition.
	MpiGetSignedBit(MiddleResult, &SignedBit);

	if(SignedBit == 0x0)
		goto condition_others;


	// Right MPI is less than left MPI.
	return MPI_YES;


condition_others:


	// Other conditions.
	return MPI_NO;
}





/**

@brief   Minimize multiple precision signed integer bit number.

Use MpiMinimizeBitNum() to minimize multiple precision signed integer MPI bit number.


@param [in]   pMpiVar   Pointer to an allocated memory for storing result

*/
void
MpiMinimizeBitNum(
	MPI *pMpiVar
	)
{
	int i;
	unsigned char SignedBit;
	unsigned char BitValue;



	// Get signed bit form MPI;
	MpiGetSignedBit(*pMpiVar, &SignedBit);


	// Find MPI MSB position.
	// Note: The MSB of signed integer is the rightest signed bit.
	for(i = (MPI_VALUE_BIT_NUM_MAX - 2); i > -1; i--)
	{
		// Get current bit value.
		MpiGetBit(*pMpiVar, i, &BitValue);

		// Compare current bit with signed bit.
		if(BitValue != SignedBit)
			break;
	}


	// Set MPI bit number.
	// Note: MPI bit number must be greater than one.
	pMpiVar->BitNum = (i == -1) ? 2 : (i + 2);


	return;
}






/**

@brief   Calculate multiple precision signed integer logarithm with base 2.

Use MpiMinimizeBitNum() to calculate multiple precision signed integer logarithm with base 2.


@param [out]   pResult            Pointer to an allocated memory for storing result (unit: pow(2, - ResultFracBitNum))
@param [in]    MpiVar             MPI variable for calculating
@param [in]    ResultFracBitNum   Result fraction bit number


@note
	-# MPI variable bit number must be minimized.
	-# MPI variable bit number must be less than (MPI_VALUE_BIT_NUM_MAX / 2 + 1).
    -# MPI variable must be greater than zero.
	-# If MPI variable is zero, the result is zero in MpiLog2().
	-# The result bit number will be minimized in MpiLog2().

*/
void
MpiLog2(
	MPI *pResult,
	MPI MpiVar,
	unsigned long ResultFracBitNum
	)
{
	unsigned int i;
	MPI MiddleResult;
	unsigned char BitValue;



	// Get integer part of MPI logarithm result with base 2.
	MpiSetValue(pResult, (long)(MpiVar.BitNum - 2));


	// Get fraction part of MPI logarithm result with base 2 by logarithm algorithm.
	// Note: Take middle result format as follows:
	//                         x x . x x ~ x
	//       (integer part 2 bits) . (fraction part MPI_LOG_MIDDLE_RESULT_FRAC_BIT_NUM bits)
	
	// Set middle result with initial value.
	MpiLeftShift(&MiddleResult, MpiVar, (MPI_LOG_MIDDLE_RESULT_FRAC_BIT_NUM - MpiVar.BitNum + 2));

	// Calculate result fraction bits.
	for(i = 0; i < ResultFracBitNum; i++)
	{
		// Shift result toward left with one bit.
		// Note: MpiLeftShift() will minimize result bit number.
		MpiLeftShift(pResult, *pResult, 1);

		// Square middle result.
		MpiMul(&MiddleResult, MiddleResult, MiddleResult);

		// Shift middle result toward right with fraction bit num.
		MpiRightShift(&MiddleResult, MiddleResult, MPI_LOG_MIDDLE_RESULT_FRAC_BIT_NUM);

		// Get middle result integer part bit 1.
		MpiGetBit(MiddleResult, MPI_LOG_MIDDLE_RESULT_FRAC_BIT_NUM + 1, &BitValue);

		// If middle result integer part bit 1 is equal to 0x1,
		// shift middle result with one bit toward right and set result LSB with one.
		if(BitValue == 0x1)
		{
			MpiRightShift(&MiddleResult, MiddleResult, 1);
			MpiSetBit(pResult, 0, 0x1);
		}
	}


	return;
}












