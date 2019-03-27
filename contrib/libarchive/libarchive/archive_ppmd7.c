/* Ppmd7.c -- PPMdH codec
2010-03-12 : Igor Pavlov : Public domain
This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain */

#include "archive_platform.h"

#include <memory.h>

#include "archive_ppmd7_private.h"

#ifdef PPMD_32BIT
  #define Ppmd7_GetPtr(p, ptr) (ptr)
  #define Ppmd7_GetContext(p, ptr) (ptr)
  #define Ppmd7_GetStats(p, ctx) ((ctx)->Stats)
#else
  #define Ppmd7_GetPtr(p, offs) ((void *)((p)->Base + (offs)))
  #define Ppmd7_GetContext(p, offs) ((CPpmd7_Context *)Ppmd7_GetPtr((p), (offs)))
  #define Ppmd7_GetStats(p, ctx) ((CPpmd_State *)Ppmd7_GetPtr((p), ((ctx)->Stats)))
#endif

#define Ppmd7_GetBinSumm(p) \
    &p->BinSumm[Ppmd7Context_OneState(p->MinContext)->Freq - 1][p->PrevSuccess + \
    p->NS2BSIndx[Ppmd7_GetContext(p, p->MinContext->Suffix)->NumStats - 1] + \
    (p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol]) + \
    2 * p->HB2Flag[Ppmd7Context_OneState(p->MinContext)->Symbol] + \
    ((p->RunLength >> 26) & 0x20)]

#define kTopValue (1 << 24)
#define MAX_FREQ 124
#define UNIT_SIZE 12

#define U2B(nu) ((UInt32)(nu) * UNIT_SIZE)
#define U2I(nu) (p->Units2Indx[(nu) - 1])
#define I2U(indx) (p->Indx2Units[indx])

#ifdef PPMD_32BIT
  #define REF(ptr) (ptr)
#else
  #define REF(ptr) ((UInt32)((Byte *)(ptr) - (p)->Base))
#endif

#define STATS_REF(ptr) ((CPpmd_State_Ref)REF(ptr))

#define CTX(ref) ((CPpmd7_Context *)Ppmd7_GetContext(p, ref))
#define STATS(ctx) Ppmd7_GetStats(p, ctx)
#define ONE_STATE(ctx) Ppmd7Context_OneState(ctx)
#define SUFFIX(ctx) CTX((ctx)->Suffix)

static const UInt16 kInitBinEsc[] = { 0x3CDD, 0x1F3F, 0x59BF, 0x48F3, 0x64A1, 0x5ABC, 0x6632, 0x6051};
static const Byte PPMD7_kExpEscape[16] = { 25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2 };

typedef CPpmd7_Context * CTX_PTR;

struct CPpmd7_Node_;

typedef
  #ifdef PPMD_32BIT
    struct CPpmd7_Node_ *
  #else
    UInt32
  #endif
  CPpmd7_Node_Ref;

typedef struct CPpmd7_Node_
{
  UInt16 Stamp; /* must be at offset 0 as CPpmd7_Context::NumStats. Stamp=0 means free */
  UInt16 NU;
  CPpmd7_Node_Ref Next; /* must be at offset >= 4 */
  CPpmd7_Node_Ref Prev;
} CPpmd7_Node;

#ifdef PPMD_32BIT
  #define NODE(ptr) (ptr)
#else
  #define NODE(offs) ((CPpmd7_Node *)(p->Base + (offs)))
#endif

static void Ppmd7_Update1(CPpmd7 *p);
static void Ppmd7_Update1_0(CPpmd7 *p);
static void Ppmd7_Update2(CPpmd7 *p);
static void Ppmd7_UpdateBin(CPpmd7 *p);
static CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked,
                                    UInt32 *scale);

/* ----------- Base ----------- */

static void Ppmd7_Construct(CPpmd7 *p)
{
  unsigned i, k, m;

  p->Base = 0;

  for (i = 0, k = 0; i < PPMD_NUM_INDEXES; i++)
  {
    unsigned step = (i >= 12 ? 4 : (i >> 2) + 1);
    do { p->Units2Indx[k++] = (Byte)i; } while(--step);
    p->Indx2Units[i] = (Byte)k;
  }

  p->NS2BSIndx[0] = (0 << 1);
  p->NS2BSIndx[1] = (1 << 1);
  memset(p->NS2BSIndx + 2, (2 << 1), 9);
  memset(p->NS2BSIndx + 11, (3 << 1), 256 - 11);

  for (i = 0; i < 3; i++)
    p->NS2Indx[i] = (Byte)i;
  for (m = i, k = 1; i < 256; i++)
  {
    p->NS2Indx[i] = (Byte)m;
    if (--k == 0)
      k = (++m) - 2;
  }

  memset(p->HB2Flag, 0, 0x40);
  memset(p->HB2Flag + 0x40, 8, 0x100 - 0x40);
}

static void Ppmd7_Free(CPpmd7 *p)
{
  free(p->Base);
  p->Size = 0;
  p->Base = 0;
}

static Bool Ppmd7_Alloc(CPpmd7 *p, UInt32 size)
{
  if (p->Base == 0 || p->Size != size)
  {
    /* RestartModel() below assumes that p->Size >= UNIT_SIZE
       (see the calculation of m->MinContext). */
    if (size < UNIT_SIZE) {
      return False;
    }
    Ppmd7_Free(p);
    p->AlignOffset =
      #ifdef PPMD_32BIT
        (4 - size) & 3;
      #else
        4 - (size & 3);
      #endif
    if ((p->Base = (Byte *)malloc(p->AlignOffset + size
        #ifndef PPMD_32BIT
        + UNIT_SIZE
        #endif
        )) == 0)
      return False;
    p->Size = size;
  }
  return True;
}

static void InsertNode(CPpmd7 *p, void *node, unsigned indx)
{
  *((CPpmd_Void_Ref *)node) = p->FreeList[indx];
  p->FreeList[indx] = REF(node);
}

static void *RemoveNode(CPpmd7 *p, unsigned indx)
{
  CPpmd_Void_Ref *node = (CPpmd_Void_Ref *)Ppmd7_GetPtr(p, p->FreeList[indx]);
  p->FreeList[indx] = *node;
  return node;
}

static void SplitBlock(CPpmd7 *p, void *ptr, unsigned oldIndx, unsigned newIndx)
{
  unsigned i, nu = I2U(oldIndx) - I2U(newIndx);
  ptr = (Byte *)ptr + U2B(I2U(newIndx));
  if (I2U(i = U2I(nu)) != nu)
  {
    unsigned k = I2U(--i);
    InsertNode(p, ((Byte *)ptr) + U2B(k), nu - k - 1);
  }
  InsertNode(p, ptr, i);
}

static void GlueFreeBlocks(CPpmd7 *p)
{
  #ifdef PPMD_32BIT
  CPpmd7_Node headItem;
  CPpmd7_Node_Ref head = &headItem;
  #else
  CPpmd7_Node_Ref head = p->AlignOffset + p->Size;
  #endif
  
  CPpmd7_Node_Ref n = head;
  unsigned i;

  p->GlueCount = 255;

  /* create doubly-linked list of free blocks */
  for (i = 0; i < PPMD_NUM_INDEXES; i++)
  {
    UInt16 nu = I2U(i);
    CPpmd7_Node_Ref next = (CPpmd7_Node_Ref)p->FreeList[i];
    p->FreeList[i] = 0;
    while (next != 0)
    {
      CPpmd7_Node *node = NODE(next);
      node->Next = n;
      n = NODE(n)->Prev = next;
      next = *(const CPpmd7_Node_Ref *)node;
      node->Stamp = 0;
      node->NU = (UInt16)nu;
    }
  }
  NODE(head)->Stamp = 1;
  NODE(head)->Next = n;
  NODE(n)->Prev = head;
  if (p->LoUnit != p->HiUnit)
    ((CPpmd7_Node *)p->LoUnit)->Stamp = 1;
  
  /* Glue free blocks */
  while (n != head)
  {
    CPpmd7_Node *node = NODE(n);
    UInt32 nu = (UInt32)node->NU;
    for (;;)
    {
      CPpmd7_Node *node2 = NODE(n) + nu;
      nu += node2->NU;
      if (node2->Stamp != 0 || nu >= 0x10000)
        break;
      NODE(node2->Prev)->Next = node2->Next;
      NODE(node2->Next)->Prev = node2->Prev;
      node->NU = (UInt16)nu;
    }
    n = node->Next;
  }
  
  /* Fill lists of free blocks */
  for (n = NODE(head)->Next; n != head;)
  {
    CPpmd7_Node *node = NODE(n);
    unsigned nu;
    CPpmd7_Node_Ref next = node->Next;
    for (nu = node->NU; nu > 128; nu -= 128, node += 128)
      InsertNode(p, node, PPMD_NUM_INDEXES - 1);
    if (I2U(i = U2I(nu)) != nu)
    {
      unsigned k = I2U(--i);
      InsertNode(p, node + k, nu - k - 1);
    }
    InsertNode(p, node, i);
    n = next;
  }
}

static void *AllocUnitsRare(CPpmd7 *p, unsigned indx)
{
  unsigned i;
  void *retVal;
  if (p->GlueCount == 0)
  {
    GlueFreeBlocks(p);
    if (p->FreeList[indx] != 0)
      return RemoveNode(p, indx);
  }
  i = indx;
  do
  {
    if (++i == PPMD_NUM_INDEXES)
    {
      UInt32 numBytes = U2B(I2U(indx));
      p->GlueCount--;
      return ((UInt32)(p->UnitsStart - p->Text) > numBytes) ? (p->UnitsStart -= numBytes) : (NULL);
    }
  }
  while (p->FreeList[i] == 0);
  retVal = RemoveNode(p, i);
  SplitBlock(p, retVal, i, indx);
  return retVal;
}

static void *AllocUnits(CPpmd7 *p, unsigned indx)
{
  UInt32 numBytes;
  if (p->FreeList[indx] != 0)
    return RemoveNode(p, indx);
  numBytes = U2B(I2U(indx));
  if (numBytes <= (UInt32)(p->HiUnit - p->LoUnit))
  {
    void *retVal = p->LoUnit;
    p->LoUnit += numBytes;
    return retVal;
  }
  return AllocUnitsRare(p, indx);
}

#define MyMem12Cpy(dest, src, num) \
  { UInt32 *d = (UInt32 *)dest; const UInt32 *s = (const UInt32 *)src; UInt32 n = num; \
    do { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; s += 3; d += 3; } while(--n); }

static void *ShrinkUnits(CPpmd7 *p, void *oldPtr, unsigned oldNU, unsigned newNU)
{
  unsigned i0 = U2I(oldNU);
  unsigned i1 = U2I(newNU);
  if (i0 == i1)
    return oldPtr;
  if (p->FreeList[i1] != 0)
  {
    void *ptr = RemoveNode(p, i1);
    MyMem12Cpy(ptr, oldPtr, newNU);
    InsertNode(p, oldPtr, i0);
    return ptr;
  }
  SplitBlock(p, oldPtr, i0, i1);
  return oldPtr;
}

#define SUCCESSOR(p) ((CPpmd_Void_Ref)((p)->SuccessorLow | ((UInt32)(p)->SuccessorHigh << 16)))

static void SetSuccessor(CPpmd_State *p, CPpmd_Void_Ref v)
{
  (p)->SuccessorLow = (UInt16)((UInt32)(v) & 0xFFFF);
  (p)->SuccessorHigh = (UInt16)(((UInt32)(v) >> 16) & 0xFFFF);
}

static void RestartModel(CPpmd7 *p)
{
  unsigned i, k, m;

  memset(p->FreeList, 0, sizeof(p->FreeList));
  p->Text = p->Base + p->AlignOffset;
  p->HiUnit = p->Text + p->Size;
  p->LoUnit = p->UnitsStart = p->HiUnit - p->Size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
  p->GlueCount = 0;

  p->OrderFall = p->MaxOrder;
  p->RunLength = p->InitRL = -(Int32)((p->MaxOrder < 12) ? p->MaxOrder : 12) - 1;
  p->PrevSuccess = 0;

  p->MinContext = p->MaxContext = (CTX_PTR)(p->HiUnit -= UNIT_SIZE); /* AllocContext(p); */
  p->MinContext->Suffix = 0;
  p->MinContext->NumStats = 256;
  p->MinContext->SummFreq = 256 + 1;
  p->FoundState = (CPpmd_State *)p->LoUnit; /* AllocUnits(p, PPMD_NUM_INDEXES - 1); */
  p->LoUnit += U2B(256 / 2);
  p->MinContext->Stats = REF(p->FoundState);
  for (i = 0; i < 256; i++)
  {
    CPpmd_State *s = &p->FoundState[i];
    s->Symbol = (Byte)i;
    s->Freq = 1;
    SetSuccessor(s, 0);
  }

  for (i = 0; i < 128; i++)
    for (k = 0; k < 8; k++)
    {
      UInt16 *dest = p->BinSumm[i] + k;
      UInt16 val = (UInt16)(PPMD_BIN_SCALE - kInitBinEsc[k] / (i + 2));
      for (m = 0; m < 64; m += 8)
        dest[m] = val;
    }
  
  for (i = 0; i < 25; i++)
    for (k = 0; k < 16; k++)
    {
      CPpmd_See *s = &p->See[i][k];
      s->Summ = (UInt16)((5 * i + 10) << (s->Shift = PPMD_PERIOD_BITS - 4));
      s->Count = 4;
    }
}

static void Ppmd7_Init(CPpmd7 *p, unsigned maxOrder)
{
  p->MaxOrder = maxOrder;
  RestartModel(p);
  p->DummySee.Shift = PPMD_PERIOD_BITS;
  p->DummySee.Summ = 0; /* unused */
  p->DummySee.Count = 64; /* unused */
}

static CTX_PTR CreateSuccessors(CPpmd7 *p, Bool skip)
{
  CPpmd_State upState;
  CTX_PTR c = p->MinContext;
  CPpmd_Byte_Ref upBranch = (CPpmd_Byte_Ref)SUCCESSOR(p->FoundState);
  CPpmd_State *ps[PPMD7_MAX_ORDER];
  unsigned numPs = 0;
  
  if (!skip)
    ps[numPs++] = p->FoundState;
  
  while (c->Suffix)
  {
    CPpmd_Void_Ref successor;
    CPpmd_State *s;
    c = SUFFIX(c);
    if (c->NumStats != 1)
    {
      for (s = STATS(c); s->Symbol != p->FoundState->Symbol; s++);
    }
    else
      s = ONE_STATE(c);
    successor = SUCCESSOR(s);
    if (successor != upBranch)
    {
      c = CTX(successor);
      if (numPs == 0)
        return c;
      break;
    }
    ps[numPs++] = s;
  }
  
  upState.Symbol = *(const Byte *)Ppmd7_GetPtr(p, upBranch);
  SetSuccessor(&upState, upBranch + 1);
  
  if (c->NumStats == 1)
    upState.Freq = ONE_STATE(c)->Freq;
  else
  {
    UInt32 cf, s0;
    CPpmd_State *s;
    for (s = STATS(c); s->Symbol != upState.Symbol; s++);
    cf = s->Freq - 1;
    s0 = c->SummFreq - c->NumStats - cf;
    upState.Freq = (Byte)(1 + ((2 * cf <= s0) ? (5 * cf > s0) : ((2 * cf + 3 * s0 - 1) / (2 * s0))));
  }

  while (numPs != 0)
  {
    /* Create Child */
    CTX_PTR c1; /* = AllocContext(p); */
    if (p->HiUnit != p->LoUnit)
      c1 = (CTX_PTR)(p->HiUnit -= UNIT_SIZE);
    else if (p->FreeList[0] != 0)
      c1 = (CTX_PTR)RemoveNode(p, 0);
    else
    {
      c1 = (CTX_PTR)AllocUnitsRare(p, 0);
      if (!c1)
        return NULL;
    }
    c1->NumStats = 1;
    *ONE_STATE(c1) = upState;
    c1->Suffix = REF(c);
    SetSuccessor(ps[--numPs], REF(c1));
    c = c1;
  }
  
  return c;
}

static void SwapStates(CPpmd_State *t1, CPpmd_State *t2)
{
  CPpmd_State tmp = *t1;
  *t1 = *t2;
  *t2 = tmp;
}

static void UpdateModel(CPpmd7 *p)
{
  CPpmd_Void_Ref successor, fSuccessor = SUCCESSOR(p->FoundState);
  CTX_PTR c;
  unsigned s0, ns;
  
  if (p->FoundState->Freq < MAX_FREQ / 4 && p->MinContext->Suffix != 0)
  {
    c = SUFFIX(p->MinContext);
    
    if (c->NumStats == 1)
    {
      CPpmd_State *s = ONE_STATE(c);
      if (s->Freq < 32)
        s->Freq++;
    }
    else
    {
      CPpmd_State *s = STATS(c);
      if (s->Symbol != p->FoundState->Symbol)
      {
        do { s++; } while (s->Symbol != p->FoundState->Symbol);
        if (s[0].Freq >= s[-1].Freq)
        {
          SwapStates(&s[0], &s[-1]);
          s--;
        }
      }
      if (s->Freq < MAX_FREQ - 9)
      {
        s->Freq += 2;
        c->SummFreq += 2;
      }
    }
  }

  if (p->OrderFall == 0)
  {
    p->MinContext = p->MaxContext = CreateSuccessors(p, True);
    if (p->MinContext == 0)
    {
      RestartModel(p);
      return;
    }
    SetSuccessor(p->FoundState, REF(p->MinContext));
    return;
  }
  
  *p->Text++ = p->FoundState->Symbol;
  successor = REF(p->Text);
  if (p->Text >= p->UnitsStart)
  {
    RestartModel(p);
    return;
  }
  
  if (fSuccessor)
  {
    if (fSuccessor <= successor)
    {
      CTX_PTR cs = CreateSuccessors(p, False);
      if (cs == NULL)
      {
        RestartModel(p);
        return;
      }
      fSuccessor = REF(cs);
    }
    if (--p->OrderFall == 0)
    {
      successor = fSuccessor;
      p->Text -= (p->MaxContext != p->MinContext);
    }
  }
  else
  {
    SetSuccessor(p->FoundState, successor);
    fSuccessor = REF(p->MinContext);
  }
  
  s0 = p->MinContext->SummFreq - (ns = p->MinContext->NumStats) - (p->FoundState->Freq - 1);
  
  for (c = p->MaxContext; c != p->MinContext; c = SUFFIX(c))
  {
    unsigned ns1;
    UInt32 cf, sf;
    if ((ns1 = c->NumStats) != 1)
    {
      if ((ns1 & 1) == 0)
      {
        /* Expand for one UNIT */
        unsigned oldNU = ns1 >> 1;
        unsigned i = U2I(oldNU);
        if (i != U2I(oldNU + 1))
        {
          void *ptr = AllocUnits(p, i + 1);
          void *oldPtr;
          if (!ptr)
          {
            RestartModel(p);
            return;
          }
          oldPtr = STATS(c);
          MyMem12Cpy(ptr, oldPtr, oldNU);
          InsertNode(p, oldPtr, i);
          c->Stats = STATS_REF(ptr);
        }
      }
      c->SummFreq = (UInt16)(c->SummFreq + (2 * ns1 < ns) + 2 * ((4 * ns1 <= ns) & (c->SummFreq <= 8 * ns1)));
    }
    else
    {
      CPpmd_State *s = (CPpmd_State*)AllocUnits(p, 0);
      if (!s)
      {
        RestartModel(p);
        return;
      }
      *s = *ONE_STATE(c);
      c->Stats = REF(s);
      if (s->Freq < MAX_FREQ / 4 - 1)
        s->Freq <<= 1;
      else
        s->Freq = MAX_FREQ - 4;
      c->SummFreq = (UInt16)(s->Freq + p->InitEsc + (ns > 3));
    }
    cf = 2 * (UInt32)p->FoundState->Freq * (c->SummFreq + 6);
    sf = (UInt32)s0 + c->SummFreq;
    if (cf < 6 * sf)
    {
      cf = 1 + (cf > sf) + (cf >= 4 * sf);
      c->SummFreq += 3;
    }
    else
    {
      cf = 4 + (cf >= 9 * sf) + (cf >= 12 * sf) + (cf >= 15 * sf);
      c->SummFreq = (UInt16)(c->SummFreq + cf);
    }
    {
      CPpmd_State *s = STATS(c) + ns1;
      SetSuccessor(s, successor);
      s->Symbol = p->FoundState->Symbol;
      s->Freq = (Byte)cf;
      c->NumStats = (UInt16)(ns1 + 1);
    }
  }
  p->MaxContext = p->MinContext = CTX(fSuccessor);
}
  
static void Rescale(CPpmd7 *p)
{
  unsigned i, adder, sumFreq, escFreq;
  CPpmd_State *stats = STATS(p->MinContext);
  CPpmd_State *s = p->FoundState;
  {
    CPpmd_State tmp = *s;
    for (; s != stats; s--)
      s[0] = s[-1];
    *s = tmp;
  }
  escFreq = p->MinContext->SummFreq - s->Freq;
  s->Freq += 4;
  adder = (p->OrderFall != 0);
  s->Freq = (Byte)((s->Freq + adder) >> 1);
  sumFreq = s->Freq;
  
  i = p->MinContext->NumStats - 1;
  do
  {
    escFreq -= (++s)->Freq;
    s->Freq = (Byte)((s->Freq + adder) >> 1);
    sumFreq += s->Freq;
    if (s[0].Freq > s[-1].Freq)
    {
      CPpmd_State *s1 = s;
      CPpmd_State tmp = *s1;
      do
        s1[0] = s1[-1];
      while (--s1 != stats && tmp.Freq > s1[-1].Freq);
      *s1 = tmp;
    }
  }
  while (--i);
  
  if (s->Freq == 0)
  {
    unsigned numStats = p->MinContext->NumStats;
    unsigned n0, n1;
    do { i++; } while ((--s)->Freq == 0);
    escFreq += i;
    p->MinContext->NumStats = (UInt16)(p->MinContext->NumStats - i);
    if (p->MinContext->NumStats == 1)
    {
      CPpmd_State tmp = *stats;
      do
      {
        tmp.Freq = (Byte)(tmp.Freq - (tmp.Freq >> 1));
        escFreq >>= 1;
      }
      while (escFreq > 1);
      InsertNode(p, stats, U2I(((numStats + 1) >> 1)));
      *(p->FoundState = ONE_STATE(p->MinContext)) = tmp;
      return;
    }
    n0 = (numStats + 1) >> 1;
    n1 = (p->MinContext->NumStats + 1) >> 1;
    if (n0 != n1)
      p->MinContext->Stats = STATS_REF(ShrinkUnits(p, stats, n0, n1));
  }
  p->MinContext->SummFreq = (UInt16)(sumFreq + escFreq - (escFreq >> 1));
  p->FoundState = STATS(p->MinContext);
}

static CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked, UInt32 *escFreq)
{
  CPpmd_See *see;
  unsigned nonMasked = p->MinContext->NumStats - numMasked;
  if (p->MinContext->NumStats != 256)
  {
    see = p->See[p->NS2Indx[nonMasked - 1]] +
        (nonMasked < (unsigned)SUFFIX(p->MinContext)->NumStats - p->MinContext->NumStats) +
        2 * (p->MinContext->SummFreq < 11 * p->MinContext->NumStats) +
        4 * (numMasked > nonMasked) +
        p->HiBitsFlag;
    {
      unsigned r = (see->Summ >> see->Shift);
      see->Summ = (UInt16)(see->Summ - r);
      *escFreq = r + (r == 0);
    }
  }
  else
  {
    see = &p->DummySee;
    *escFreq = 1;
  }
  return see;
}

static void NextContext(CPpmd7 *p)
{
  CTX_PTR c = CTX(SUCCESSOR(p->FoundState));
  if (p->OrderFall == 0 && (Byte *)c > p->Text)
    p->MinContext = p->MaxContext = c;
  else
    UpdateModel(p);
}

static void Ppmd7_Update1(CPpmd7 *p)
{
  CPpmd_State *s = p->FoundState;
  s->Freq += 4;
  p->MinContext->SummFreq += 4;
  if (s[0].Freq > s[-1].Freq)
  {
    SwapStates(&s[0], &s[-1]);
    p->FoundState = --s;
    if (s->Freq > MAX_FREQ)
      Rescale(p);
  }
  NextContext(p);
}

static void Ppmd7_Update1_0(CPpmd7 *p)
{
  p->PrevSuccess = (2 * p->FoundState->Freq > p->MinContext->SummFreq);
  p->RunLength += p->PrevSuccess;
  p->MinContext->SummFreq += 4;
  if ((p->FoundState->Freq += 4) > MAX_FREQ)
    Rescale(p);
  NextContext(p);
}

static void Ppmd7_UpdateBin(CPpmd7 *p)
{
  p->FoundState->Freq = (Byte)(p->FoundState->Freq + (p->FoundState->Freq < 128 ? 1: 0));
  p->PrevSuccess = 1;
  p->RunLength++;
  NextContext(p);
}

static void Ppmd7_Update2(CPpmd7 *p)
{
  p->MinContext->SummFreq += 4;
  if ((p->FoundState->Freq += 4) > MAX_FREQ)
    Rescale(p);
  p->RunLength = p->InitRL;
  UpdateModel(p);
}

/* ---------- Decode ---------- */

static Bool Ppmd_RangeDec_Init(CPpmd7z_RangeDec *p)
{
  unsigned i;
  p->Low = p->Bottom = 0;
  p->Range = 0xFFFFFFFF;
  for (i = 0; i < 4; i++)
    p->Code = (p->Code << 8) | p->Stream->Read((void *)p->Stream);
  return (p->Code < 0xFFFFFFFF);
}

static Bool Ppmd7z_RangeDec_Init(CPpmd7z_RangeDec *p)
{
  if (p->Stream->Read((void *)p->Stream) != 0)
    return False;
  return Ppmd_RangeDec_Init(p);
}

static Bool PpmdRAR_RangeDec_Init(CPpmd7z_RangeDec *p)
{
  if (!Ppmd_RangeDec_Init(p))
    return False;
  p->Bottom = 0x8000;
  return True;
}

static UInt32 Range_GetThreshold(void *pp, UInt32 total)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  return (p->Code - p->Low) / (p->Range /= total);
}

static void Range_Normalize(CPpmd7z_RangeDec *p)
{
  while (1)
  {
    if((p->Low ^ (p->Low + p->Range)) >= kTopValue)
    {
      if(p->Range >= p->Bottom)
        break;
      else
        p->Range = ((uint32_t)(-(int32_t)p->Low)) & (p->Bottom - 1);
    }
    p->Code = (p->Code << 8) | p->Stream->Read((void *)p->Stream);
    p->Range <<= 8;
    p->Low <<= 8;
  }
}

static void Range_Decode_7z(void *pp, UInt32 start, UInt32 size)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  p->Code -= start * p->Range;
  p->Range *= size;
  Range_Normalize(p);
}

static void Range_Decode_RAR(void *pp, UInt32 start, UInt32 size)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  p->Low += start * p->Range;
  p->Range *= size;
  Range_Normalize(p);
}

static UInt32 Range_DecodeBit_7z(void *pp, UInt32 size0)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  UInt32 newBound = (p->Range >> 14) * size0;
  UInt32 symbol;
  if (p->Code < newBound)
  {
    symbol = 0;
    p->Range = newBound;
  }
  else
  {
    symbol = 1;
    p->Code -= newBound;
    p->Range -= newBound;
  }
  Range_Normalize(p);
  return symbol;
}

static UInt32 Range_DecodeBit_RAR(void *pp, UInt32 size0)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  UInt32 bit, value = p->p.GetThreshold(p, PPMD_BIN_SCALE);
  if(value < size0)
  {
    bit = 0;
    p->p.Decode(p, 0, size0);
  }
  else
  {
    bit = 1;
    p->p.Decode(p, size0, PPMD_BIN_SCALE - size0);
  }
  return bit;
}

static void Ppmd7z_RangeDec_CreateVTable(CPpmd7z_RangeDec *p)
{
  p->p.GetThreshold = Range_GetThreshold;
  p->p.Decode = Range_Decode_7z;
  p->p.DecodeBit = Range_DecodeBit_7z;
}

static void PpmdRAR_RangeDec_CreateVTable(CPpmd7z_RangeDec *p)
{
  p->p.GetThreshold = Range_GetThreshold;
  p->p.Decode = Range_Decode_RAR;
  p->p.DecodeBit = Range_DecodeBit_RAR;
}

#define MASK(sym) ((signed char *)charMask)[sym]

static int Ppmd7_DecodeSymbol(CPpmd7 *p, IPpmd7_RangeDec *rc)
{
  size_t charMask[256 / sizeof(size_t)];
  if (p->MinContext->NumStats != 1)
  {
    CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
    unsigned i;
    UInt32 count, hiCnt;
    if ((count = rc->GetThreshold(rc, p->MinContext->SummFreq)) < (hiCnt = s->Freq))
    {
      Byte symbol;
      rc->Decode(rc, 0, s->Freq);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd7_Update1_0(p);
      return symbol;
    }
    p->PrevSuccess = 0;
    i = p->MinContext->NumStats - 1;
    do
    {
      if ((hiCnt += (++s)->Freq) > count)
      {
        Byte symbol;
        rc->Decode(rc, hiCnt - s->Freq, s->Freq);
        p->FoundState = s;
        symbol = s->Symbol;
        Ppmd7_Update1(p);
        return symbol;
      }
    }
    while (--i);
    if (count >= p->MinContext->SummFreq)
      return -2;
    p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
    rc->Decode(rc, hiCnt, p->MinContext->SummFreq - hiCnt);
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats - 1;
    do { MASK((--s)->Symbol) = 0; } while (--i);
  }
  else
  {
    UInt16 *prob = Ppmd7_GetBinSumm(p);
    if (rc->DecodeBit(rc, *prob) == 0)
    {
      Byte symbol;
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      symbol = (p->FoundState = Ppmd7Context_OneState(p->MinContext))->Symbol;
      Ppmd7_UpdateBin(p);
      return symbol;
    }
    *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
    p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(Ppmd7Context_OneState(p->MinContext)->Symbol) = 0;
    p->PrevSuccess = 0;
  }
  for (;;)
  {
    CPpmd_State *ps[256], *s;
    UInt32 freqSum, count, hiCnt;
    CPpmd_See *see;
    unsigned i, num, numMasked = p->MinContext->NumStats;
    do
    {
      p->OrderFall++;
      if (!p->MinContext->Suffix)
        return -1;
      p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    hiCnt = 0;
    s = Ppmd7_GetStats(p, p->MinContext);
    i = 0;
    num = p->MinContext->NumStats - numMasked;
    do
    {
      int k = (int)(MASK(s->Symbol));
      hiCnt += (s->Freq & k);
      ps[i] = s++;
      i -= k;
    }
    while (i != num);

    see = Ppmd7_MakeEscFreq(p, numMasked, &freqSum);
    freqSum += hiCnt;
    count = rc->GetThreshold(rc, freqSum);

    if (count < hiCnt)
    {
      Byte symbol;
      CPpmd_State **pps = ps;
      for (hiCnt = 0; (hiCnt += (*pps)->Freq) <= count; pps++);
      s = *pps;
      rc->Decode(rc, hiCnt - s->Freq, s->Freq);
      Ppmd_See_Update(see);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd7_Update2(p);
      return symbol;
    }
    if (count >= freqSum)
      return -2;
    rc->Decode(rc, hiCnt, freqSum - hiCnt);
    see->Summ = (UInt16)(see->Summ + freqSum);
    do { MASK(ps[--i]->Symbol) = 0; } while (i != 0);
  }
}

/* ---------- Encode ---------- Ppmd7Enc.c */

#define kTopValue (1 << 24)

static void Ppmd7z_RangeEnc_Init(CPpmd7z_RangeEnc *p)
{
  p->Low = 0;
  p->Range = 0xFFFFFFFF;
  p->Cache = 0;
  p->CacheSize = 1;
}

static void RangeEnc_ShiftLow(CPpmd7z_RangeEnc *p)
{
  if ((UInt32)p->Low < (UInt32)0xFF000000 || (unsigned)(p->Low >> 32) != 0)
  {
    Byte temp = p->Cache;
    do
    {
      p->Stream->Write(p->Stream, (Byte)(temp + (Byte)(p->Low >> 32)));
      temp = 0xFF;
    }
    while(--p->CacheSize != 0);
    p->Cache = (Byte)((UInt32)p->Low >> 24);
  }
  p->CacheSize++;
  p->Low = ((UInt32)p->Low << 8) & 0xFFFFFFFF;
}

static void RangeEnc_Encode(CPpmd7z_RangeEnc *p, UInt32 start, UInt32 size, UInt32 total)
{
  p->Low += start * (p->Range /= total);
  p->Range *= size;
  while (p->Range < kTopValue)
  {
    p->Range <<= 8;
    RangeEnc_ShiftLow(p);
  }
}

static void RangeEnc_EncodeBit_0(CPpmd7z_RangeEnc *p, UInt32 size0)
{
  p->Range = (p->Range >> 14) * size0;
  while (p->Range < kTopValue)
  {
    p->Range <<= 8;
    RangeEnc_ShiftLow(p);
  }
}

static void RangeEnc_EncodeBit_1(CPpmd7z_RangeEnc *p, UInt32 size0)
{
  UInt32 newBound = (p->Range >> 14) * size0;
  p->Low += newBound;
  p->Range -= newBound;
  while (p->Range < kTopValue)
  {
    p->Range <<= 8;
    RangeEnc_ShiftLow(p);
  }
}

static void Ppmd7z_RangeEnc_FlushData(CPpmd7z_RangeEnc *p)
{
  unsigned i;
  for (i = 0; i < 5; i++)
    RangeEnc_ShiftLow(p);
}


#define MASK(sym) ((signed char *)charMask)[sym]

static void Ppmd7_EncodeSymbol(CPpmd7 *p, CPpmd7z_RangeEnc *rc, int symbol)
{
  size_t charMask[256 / sizeof(size_t)];
  if (p->MinContext->NumStats != 1)
  {
    CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
    UInt32 sum;
    unsigned i;
    if (s->Symbol == symbol)
    {
      RangeEnc_Encode(rc, 0, s->Freq, p->MinContext->SummFreq);
      p->FoundState = s;
      Ppmd7_Update1_0(p);
      return;
    }
    p->PrevSuccess = 0;
    sum = s->Freq;
    i = p->MinContext->NumStats - 1;
    do
    {
      if ((++s)->Symbol == symbol)
      {
        RangeEnc_Encode(rc, sum, s->Freq, p->MinContext->SummFreq);
        p->FoundState = s;
        Ppmd7_Update1(p);
        return;
      }
      sum += s->Freq;
    }
    while (--i);
    
    p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats - 1;
    do { MASK((--s)->Symbol) = 0; } while (--i);
    RangeEnc_Encode(rc, sum, p->MinContext->SummFreq - sum, p->MinContext->SummFreq);
  }
  else
  {
    UInt16 *prob = Ppmd7_GetBinSumm(p);
    CPpmd_State *s = Ppmd7Context_OneState(p->MinContext);
    if (s->Symbol == symbol)
    {
      RangeEnc_EncodeBit_0(rc, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      p->FoundState = s;
      Ppmd7_UpdateBin(p);
      return;
    }
    else
    {
      RangeEnc_EncodeBit_1(rc, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
      p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
      PPMD_SetAllBitsIn256Bytes(charMask);
      MASK(s->Symbol) = 0;
      p->PrevSuccess = 0;
    }
  }
  for (;;)
  {
    UInt32 escFreq;
    CPpmd_See *see;
    CPpmd_State *s;
    UInt32 sum;
    unsigned i, numMasked = p->MinContext->NumStats;
    do
    {
      p->OrderFall++;
      if (!p->MinContext->Suffix)
        return; /* EndMarker (symbol = -1) */
      p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    
    see = Ppmd7_MakeEscFreq(p, numMasked, &escFreq);
    s = Ppmd7_GetStats(p, p->MinContext);
    sum = 0;
    i = p->MinContext->NumStats;
    do
    {
      int cur = s->Symbol;
      if (cur == symbol)
      {
        UInt32 low = sum;
        CPpmd_State *s1 = s;
        do
        {
          sum += (s->Freq & (int)(MASK(s->Symbol)));
          s++;
        }
        while (--i);
        RangeEnc_Encode(rc, low, s1->Freq, sum + escFreq);
        Ppmd_See_Update(see);
        p->FoundState = s1;
        Ppmd7_Update2(p);
        return;
      }
      sum += (s->Freq & (int)(MASK(cur)));
      MASK(cur) = 0;
      s++;
    }
    while (--i);
    
    RangeEnc_Encode(rc, sum, escFreq, sum + escFreq);
    see->Summ = (UInt16)(see->Summ + sum + escFreq);
  }
}

const IPpmd7 __archive_ppmd7_functions =
{
  &Ppmd7_Construct,
  &Ppmd7_Alloc,
  &Ppmd7_Free,
  &Ppmd7_Init,
  &Ppmd7z_RangeDec_CreateVTable,
  &PpmdRAR_RangeDec_CreateVTable,
  &Ppmd7z_RangeDec_Init,
  &PpmdRAR_RangeDec_Init,
  &Ppmd7_DecodeSymbol,
  &Ppmd7z_RangeEnc_Init,
  &Ppmd7z_RangeEnc_FlushData,
  &Ppmd7_EncodeSymbol
};
