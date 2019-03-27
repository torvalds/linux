/* Ppmd8.c -- PPMdI codec
2016-05-21 : Igor Pavlov : Public domain
This code is based on PPMd var.I (2002): Dmitry Shkarin : Public domain */

#include "archive_platform.h"

#include <string.h>

#include "archive_ppmd8_private.h"

const Byte PPMD8_kExpEscape[16] = { 25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2 };
static const UInt16 kInitBinEsc[] = { 0x3CDD, 0x1F3F, 0x59BF, 0x48F3, 0x64A1, 0x5ABC, 0x6632, 0x6051};

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

#define CTX(ref) ((CPpmd8_Context *)Ppmd8_GetContext(p, ref))
#define STATS(ctx) Ppmd8_GetStats(p, ctx)
#define ONE_STATE(ctx) Ppmd8Context_OneState(ctx)
#define SUFFIX(ctx) CTX((ctx)->Suffix)

#define kTop (1 << 24)
#define kBot (1 << 15)

typedef CPpmd8_Context * CTX_PTR;

struct CPpmd8_Node_;

typedef
  #ifdef PPMD_32BIT
    struct CPpmd8_Node_ *
  #else
    UInt32
  #endif
  CPpmd8_Node_Ref;

typedef struct CPpmd8_Node_
{
  UInt32 Stamp;
  CPpmd8_Node_Ref Next;
  UInt32 NU;
} CPpmd8_Node;

#ifdef PPMD_32BIT
  #define NODE(ptr) (ptr)
#else
  #define NODE(offs) ((CPpmd8_Node *)(p->Base + (offs)))
#endif

#define EMPTY_NODE 0xFFFFFFFF

void Ppmd8_Construct(CPpmd8 *p)
{
  unsigned i, k, m;

  p->Base = 0;

  for (i = 0, k = 0; i < PPMD_NUM_INDEXES; i++)
  {
    unsigned step = (i >= 12 ? 4 : (i >> 2) + 1);
    do { p->Units2Indx[k++] = (Byte)i; } while (--step);
    p->Indx2Units[i] = (Byte)k;
  }

  p->NS2BSIndx[0] = (0 << 1);
  p->NS2BSIndx[1] = (1 << 1);
  memset(p->NS2BSIndx + 2, (2 << 1), 9);
  memset(p->NS2BSIndx + 11, (3 << 1), 256 - 11);

  for (i = 0; i < 5; i++)
    p->NS2Indx[i] = (Byte)i;
  for (m = i, k = 1; i < 260; i++)
  {
    p->NS2Indx[i] = (Byte)m;
    if (--k == 0)
      k = (++m) - 4;
  }
}

void Ppmd8_Free(CPpmd8 *p)
{
  free(p->Base);
  p->Size = 0;
  p->Base = 0;
}

Bool Ppmd8_Alloc(CPpmd8 *p, UInt32 size)
{
  if (p->Base == 0 || p->Size != size)
  {
    Ppmd8_Free(p);
    p->AlignOffset =
      #ifdef PPMD_32BIT
        (4 - size) & 3;
      #else
        4 - (size & 3);
      #endif
    if ((p->Base = (Byte *)malloc(p->AlignOffset + size)) == 0)
      return False;
    p->Size = size;
  }
  return True;
}

static void InsertNode(CPpmd8 *p, void *node, unsigned indx)
{
  ((CPpmd8_Node *)node)->Stamp = EMPTY_NODE;
  ((CPpmd8_Node *)node)->Next = (CPpmd8_Node_Ref)p->FreeList[indx];
  ((CPpmd8_Node *)node)->NU = I2U(indx);
  p->FreeList[indx] = REF(node);
  p->Stamps[indx]++;
}

static void *RemoveNode(CPpmd8 *p, unsigned indx)
{
  CPpmd8_Node *node = NODE((CPpmd8_Node_Ref)p->FreeList[indx]);
  p->FreeList[indx] = node->Next;
  p->Stamps[indx]--;
  return node;
}

static void SplitBlock(CPpmd8 *p, void *ptr, unsigned oldIndx, unsigned newIndx)
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

static void GlueFreeBlocks(CPpmd8 *p)
{
  CPpmd8_Node_Ref head = 0;
  CPpmd8_Node_Ref *prev = &head;
  unsigned i;

  p->GlueCount = 1 << 13;
  memset(p->Stamps, 0, sizeof(p->Stamps));
  
  /* Order-0 context is always at top UNIT, so we don't need guard NODE at the end.
     All blocks up to p->LoUnit can be free, so we need guard NODE at LoUnit. */
  if (p->LoUnit != p->HiUnit)
    ((CPpmd8_Node *)p->LoUnit)->Stamp = 0;

  /* Glue free blocks */
  for (i = 0; i < PPMD_NUM_INDEXES; i++)
  {
    CPpmd8_Node_Ref next = (CPpmd8_Node_Ref)p->FreeList[i];
    p->FreeList[i] = 0;
    while (next != 0)
    {
      CPpmd8_Node *node = NODE(next);
      if (node->NU != 0)
      {
        CPpmd8_Node *node2;
        *prev = next;
        prev = &(node->Next);
        while ((node2 = node + node->NU)->Stamp == EMPTY_NODE)
        {
          node->NU += node2->NU;
          node2->NU = 0;
        }
      }
      next = node->Next;
    }
  }
  *prev = 0;
  
  /* Fill lists of free blocks */
  while (head != 0)
  {
    CPpmd8_Node *node = NODE(head);
    unsigned nu;
    head = node->Next;
    nu = node->NU;
    if (nu == 0)
      continue;
    for (; nu > 128; nu -= 128, node += 128)
      InsertNode(p, node, PPMD_NUM_INDEXES - 1);
    if (I2U(i = U2I(nu)) != nu)
    {
      unsigned k = I2U(--i);
      InsertNode(p, node + k, nu - k - 1);
    }
    InsertNode(p, node, i);
  }
}

static void *AllocUnitsRare(CPpmd8 *p, unsigned indx)
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

static void *AllocUnits(CPpmd8 *p, unsigned indx)
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
  { UInt32 *d = (UInt32 *)dest; const UInt32 *z = (const UInt32 *)src; UInt32 n = num; \
    do { d[0] = z[0]; d[1] = z[1]; d[2] = z[2]; z += 3; d += 3; } while (--n); }

static void *ShrinkUnits(CPpmd8 *p, void *oldPtr, unsigned oldNU, unsigned newNU)
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

static void FreeUnits(CPpmd8 *p, void *ptr, unsigned nu)
{
  InsertNode(p, ptr, U2I(nu));
}

static void SpecialFreeUnit(CPpmd8 *p, void *ptr)
{
  if ((Byte *)ptr != p->UnitsStart)
    InsertNode(p, ptr, 0);
  else
  {
    #ifdef PPMD8_FREEZE_SUPPORT
    *(UInt32 *)ptr = EMPTY_NODE; /* it's used for (Flags == 0xFF) check in RemoveBinContexts */
    #endif
    p->UnitsStart += UNIT_SIZE;
  }
}

static void *MoveUnitsUp(CPpmd8 *p, void *oldPtr, unsigned nu)
{
  unsigned indx = U2I(nu);
  void *ptr;
  if ((Byte *)oldPtr > p->UnitsStart + 16 * 1024 || REF(oldPtr) > p->FreeList[indx])
    return oldPtr;
  ptr = RemoveNode(p, indx);
  MyMem12Cpy(ptr, oldPtr, nu);
  if ((Byte*)oldPtr != p->UnitsStart)
    InsertNode(p, oldPtr, indx);
  else
    p->UnitsStart += U2B(I2U(indx));
  return ptr;
}

static void ExpandTextArea(CPpmd8 *p)
{
  UInt32 count[PPMD_NUM_INDEXES];
  unsigned i;
  memset(count, 0, sizeof(count));
  if (p->LoUnit != p->HiUnit)
    ((CPpmd8_Node *)p->LoUnit)->Stamp = 0;
  
  {
    CPpmd8_Node *node = (CPpmd8_Node *)p->UnitsStart;
    for (; node->Stamp == EMPTY_NODE; node += node->NU)
    {
      node->Stamp = 0;
      count[U2I(node->NU)]++;
    }
    p->UnitsStart = (Byte *)node;
  }
  
  for (i = 0; i < PPMD_NUM_INDEXES; i++)
  {
    CPpmd8_Node_Ref *next = (CPpmd8_Node_Ref *)&p->FreeList[i];
    while (count[i] != 0)
    {
      CPpmd8_Node *node = NODE(*next);
      while (node->Stamp == 0)
      {
        *next = node->Next;
        node = NODE(*next);
        p->Stamps[i]--;
        if (--count[i] == 0)
          break;
      }
      next = &node->Next;
    }
  }
}

#define SUCCESSOR(p) ((CPpmd_Void_Ref)((p)->SuccessorLow | ((UInt32)(p)->SuccessorHigh << 16)))

static void SetSuccessor(CPpmd_State *p, CPpmd_Void_Ref v)
{
  (p)->SuccessorLow = (UInt16)((UInt32)(v) & 0xFFFF);
  (p)->SuccessorHigh = (UInt16)(((UInt32)(v) >> 16) & 0xFFFF);
}

#define RESET_TEXT(offs) { p->Text = p->Base + p->AlignOffset + (offs); }

static void RestartModel(CPpmd8 *p)
{
  unsigned i, k, m, r;

  memset(p->FreeList, 0, sizeof(p->FreeList));
  memset(p->Stamps, 0, sizeof(p->Stamps));
  RESET_TEXT(0);
  p->HiUnit = p->Text + p->Size;
  p->LoUnit = p->UnitsStart = p->HiUnit - p->Size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
  p->GlueCount = 0;

  p->OrderFall = p->MaxOrder;
  p->RunLength = p->InitRL = -(Int32)((p->MaxOrder < 12) ? p->MaxOrder : 12) - 1;
  p->PrevSuccess = 0;

  p->MinContext = p->MaxContext = (CTX_PTR)(p->HiUnit -= UNIT_SIZE); /* AllocContext(p); */
  p->MinContext->Suffix = 0;
  p->MinContext->NumStats = 255;
  p->MinContext->Flags = 0;
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

  for (i = m = 0; m < 25; m++)
  {
    while (p->NS2Indx[i] == m)
      i++;
    for (k = 0; k < 8; k++)
    {
      UInt16 val = (UInt16)(PPMD_BIN_SCALE - kInitBinEsc[k] / (i + 1));
      UInt16 *dest = p->BinSumm[m] + k;
      for (r = 0; r < 64; r += 8)
        dest[r] = val;
    }
  }

  for (i = m = 0; m < 24; m++)
  {
    while (p->NS2Indx[i + 3] == m + 3)
      i++;
    for (k = 0; k < 32; k++)
    {
      CPpmd_See *s = &p->See[m][k];
      s->Summ = (UInt16)((2 * i + 5) << (s->Shift = PPMD_PERIOD_BITS - 4));
      s->Count = 7;
    }
  }
}

void Ppmd8_Init(CPpmd8 *p, unsigned maxOrder, unsigned restoreMethod)
{
  p->MaxOrder = maxOrder;
  p->RestoreMethod = restoreMethod;
  RestartModel(p);
  p->DummySee.Shift = PPMD_PERIOD_BITS;
  p->DummySee.Summ = 0; /* unused */
  p->DummySee.Count = 64; /* unused */
}

static void Refresh(CPpmd8 *p, CTX_PTR ctx, unsigned oldNU, unsigned scale)
{
  unsigned i = ctx->NumStats, escFreq, sumFreq, flags;
  CPpmd_State *s = (CPpmd_State *)ShrinkUnits(p, STATS(ctx), oldNU, (i + 2) >> 1);
  ctx->Stats = REF(s);
  #ifdef PPMD8_FREEZE_SUPPORT
  /* fixed over Shkarin's code. Fixed code is not compatible with original code for some files in FREEZE mode. */
  scale |= (ctx->SummFreq >= ((UInt32)1 << 15));
  #endif
  flags = (ctx->Flags & (0x10 + 0x04 * scale)) + 0x08 * (s->Symbol >= 0x40);
  escFreq = ctx->SummFreq - s->Freq;
  sumFreq = (s->Freq = (Byte)((s->Freq + scale) >> scale));
  do
  {
    escFreq -= (++s)->Freq;
    sumFreq += (s->Freq = (Byte)((s->Freq + scale) >> scale));
    flags |= 0x08 * (s->Symbol >= 0x40);
  }
  while (--i);
  ctx->SummFreq = (UInt16)(sumFreq + ((escFreq + scale) >> scale));
  ctx->Flags = (Byte)flags;
}

static void SwapStates(CPpmd_State *t1, CPpmd_State *t2)
{
  CPpmd_State tmp = *t1;
  *t1 = *t2;
  *t2 = tmp;
}

static CPpmd_Void_Ref CutOff(CPpmd8 *p, CTX_PTR ctx, unsigned order)
{
  int i;
  unsigned tmp;
  CPpmd_State *s;
  
  if (!ctx->NumStats)
  {
    s = ONE_STATE(ctx);
    if ((Byte *)Ppmd8_GetPtr(p, SUCCESSOR(s)) >= p->UnitsStart)
    {
      if (order < p->MaxOrder)
        SetSuccessor(s, CutOff(p, CTX(SUCCESSOR(s)), order + 1));
      else
        SetSuccessor(s, 0);
      if (SUCCESSOR(s) || order <= 9) /* O_BOUND */
        return REF(ctx);
    }
    SpecialFreeUnit(p, ctx);
    return 0;
  }

  ctx->Stats = STATS_REF(MoveUnitsUp(p, STATS(ctx), tmp = ((unsigned)ctx->NumStats + 2) >> 1));

  for (s = STATS(ctx) + (i = ctx->NumStats); s >= STATS(ctx); s--)
    if ((Byte *)Ppmd8_GetPtr(p, SUCCESSOR(s)) < p->UnitsStart)
    {
      CPpmd_State *s2 = STATS(ctx) + (i--);
      SetSuccessor(s, 0);
      SwapStates(s, s2);
    }
    else if (order < p->MaxOrder)
      SetSuccessor(s, CutOff(p, CTX(SUCCESSOR(s)), order + 1));
    else
      SetSuccessor(s, 0);
    
  if (i != ctx->NumStats && order)
  {
    ctx->NumStats = (Byte)i;
    s = STATS(ctx);
    if (i < 0)
    {
      FreeUnits(p, s, tmp);
      SpecialFreeUnit(p, ctx);
      return 0;
    }
    if (i == 0)
    {
      ctx->Flags = (Byte)((ctx->Flags & 0x10) + 0x08 * (s->Symbol >= 0x40));
      *ONE_STATE(ctx) = *s;
      FreeUnits(p, s, tmp);
      /* 9.31: the code was fixed. It's was not BUG, if Freq <= MAX_FREQ = 124 */
      ONE_STATE(ctx)->Freq = (Byte)(((unsigned)ONE_STATE(ctx)->Freq + 11) >> 3);
    }
    else
      Refresh(p, ctx, tmp, ctx->SummFreq > 16 * i);
  }
  return REF(ctx);
}

#ifdef PPMD8_FREEZE_SUPPORT
static CPpmd_Void_Ref RemoveBinContexts(CPpmd8 *p, CTX_PTR ctx, unsigned order)
{
  CPpmd_State *s;
  if (!ctx->NumStats)
  {
    s = ONE_STATE(ctx);
    if ((Byte *)Ppmd8_GetPtr(p, SUCCESSOR(s)) >= p->UnitsStart && order < p->MaxOrder)
      SetSuccessor(s, RemoveBinContexts(p, CTX(SUCCESSOR(s)), order + 1));
    else
      SetSuccessor(s, 0);
    /* Suffix context can be removed already, since different (high-order)
       Successors may refer to same context. So we check Flags == 0xFF (Stamp == EMPTY_NODE) */
    if (!SUCCESSOR(s) && (!SUFFIX(ctx)->NumStats || SUFFIX(ctx)->Flags == 0xFF))
    {
      FreeUnits(p, ctx, 1);
      return 0;
    }
    else
      return REF(ctx);
  }

  for (s = STATS(ctx) + ctx->NumStats; s >= STATS(ctx); s--)
    if ((Byte *)Ppmd8_GetPtr(p, SUCCESSOR(s)) >= p->UnitsStart && order < p->MaxOrder)
      SetSuccessor(s, RemoveBinContexts(p, CTX(SUCCESSOR(s)), order + 1));
    else
      SetSuccessor(s, 0);
  
  return REF(ctx);
}
#endif

static UInt32 GetUsedMemory(const CPpmd8 *p)
{
  UInt32 v = 0;
  unsigned i;
  for (i = 0; i < PPMD_NUM_INDEXES; i++)
    v += p->Stamps[i] * I2U(i);
  return p->Size - (UInt32)(p->HiUnit - p->LoUnit) - (UInt32)(p->UnitsStart - p->Text) - U2B(v);
}

#ifdef PPMD8_FREEZE_SUPPORT
  #define RESTORE_MODEL(c1, fSuccessor) RestoreModel(p, c1, fSuccessor)
#else
  #define RESTORE_MODEL(c1, fSuccessor) RestoreModel(p, c1)
#endif

static void RestoreModel(CPpmd8 *p, CTX_PTR c1
    #ifdef PPMD8_FREEZE_SUPPORT
    , CTX_PTR fSuccessor
    #endif
    )
{
  CTX_PTR c;
  CPpmd_State *s;
  RESET_TEXT(0);
  for (c = p->MaxContext; c != c1; c = SUFFIX(c))
    if (--(c->NumStats) == 0)
    {
      s = STATS(c);
      c->Flags = (Byte)((c->Flags & 0x10) + 0x08 * (s->Symbol >= 0x40));
      *ONE_STATE(c) = *s;
      SpecialFreeUnit(p, s);
      ONE_STATE(c)->Freq = (Byte)(((unsigned)ONE_STATE(c)->Freq + 11) >> 3);
    }
    else
      Refresh(p, c, (c->NumStats+3) >> 1, 0);
 
  for (; c != p->MinContext; c = SUFFIX(c))
    if (!c->NumStats)
      ONE_STATE(c)->Freq = (Byte)(ONE_STATE(c)->Freq - (ONE_STATE(c)->Freq >> 1));
    else if ((c->SummFreq += 4) > 128 + 4 * c->NumStats)
      Refresh(p, c, (c->NumStats + 2) >> 1, 1);

  #ifdef PPMD8_FREEZE_SUPPORT
  if (p->RestoreMethod > PPMD8_RESTORE_METHOD_FREEZE)
  {
    p->MaxContext = fSuccessor;
    p->GlueCount += !(p->Stamps[1] & 1);
  }
  else if (p->RestoreMethod == PPMD8_RESTORE_METHOD_FREEZE)
  {
    while (p->MaxContext->Suffix)
      p->MaxContext = SUFFIX(p->MaxContext);
    RemoveBinContexts(p, p->MaxContext, 0);
    p->RestoreMethod++;
    p->GlueCount = 0;
    p->OrderFall = p->MaxOrder;
  }
  else
  #endif
  if (p->RestoreMethod == PPMD8_RESTORE_METHOD_RESTART || GetUsedMemory(p) < (p->Size >> 1))
    RestartModel(p);
  else
  {
    while (p->MaxContext->Suffix)
      p->MaxContext = SUFFIX(p->MaxContext);
    do
    {
      CutOff(p, p->MaxContext, 0);
      ExpandTextArea(p);
    }
    while (GetUsedMemory(p) > 3 * (p->Size >> 2));
    p->GlueCount = 0;
    p->OrderFall = p->MaxOrder;
  }
}

static CTX_PTR CreateSuccessors(CPpmd8 *p, Bool skip, CPpmd_State *s1, CTX_PTR c)
{
  CPpmd_State upState;
  Byte flags;
  CPpmd_Byte_Ref upBranch = (CPpmd_Byte_Ref)SUCCESSOR(p->FoundState);
  /* fixed over Shkarin's code. Maybe it could work without + 1 too. */
  CPpmd_State *ps[PPMD8_MAX_ORDER + 1];
  unsigned numPs = 0;
  
  if (!skip)
    ps[numPs++] = p->FoundState;
  
  while (c->Suffix)
  {
    CPpmd_Void_Ref successor;
    CPpmd_State *s;
    c = SUFFIX(c);
    if (s1)
    {
      s = s1;
      s1 = NULL;
    }
    else if (c->NumStats != 0)
    {
      for (s = STATS(c); s->Symbol != p->FoundState->Symbol; s++);
      if (s->Freq < MAX_FREQ - 9)
      {
        s->Freq++;
        c->SummFreq++;
      }
    }
    else
    {
      s = ONE_STATE(c);
      s->Freq = (Byte)(s->Freq + (!SUFFIX(c)->NumStats & (s->Freq < 24)));
    }
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
  
  upState.Symbol = *(const Byte *)Ppmd8_GetPtr(p, upBranch);
  SetSuccessor(&upState, upBranch + 1);
  flags = (Byte)(0x10 * (p->FoundState->Symbol >= 0x40) + 0x08 * (upState.Symbol >= 0x40));

  if (c->NumStats == 0)
    upState.Freq = ONE_STATE(c)->Freq;
  else
  {
    UInt32 cf, s0;
    CPpmd_State *s;
    for (s = STATS(c); s->Symbol != upState.Symbol; s++);
    cf = s->Freq - 1;
    s0 = c->SummFreq - c->NumStats - cf;
    upState.Freq = (Byte)(1 + ((2 * cf <= s0) ? (5 * cf > s0) : ((cf + 2 * s0 - 3) / s0)));
  }

  do
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
    c1->NumStats = 0;
    c1->Flags = flags;
    *ONE_STATE(c1) = upState;
    c1->Suffix = REF(c);
    SetSuccessor(ps[--numPs], REF(c1));
    c = c1;
  }
  while (numPs != 0);
  
  return c;
}

static CTX_PTR ReduceOrder(CPpmd8 *p, CPpmd_State *s1, CTX_PTR c)
{
  CPpmd_State *s = NULL;
  CTX_PTR c1 = c;
  CPpmd_Void_Ref upBranch = REF(p->Text);
  
  #ifdef PPMD8_FREEZE_SUPPORT
  /* The BUG in Shkarin's code was fixed: ps could overflow in CUT_OFF mode. */
  CPpmd_State *ps[PPMD8_MAX_ORDER + 1];
  unsigned numPs = 0;
  ps[numPs++] = p->FoundState;
  #endif

  SetSuccessor(p->FoundState, upBranch);
  p->OrderFall++;

  for (;;)
  {
    if (s1)
    {
      c = SUFFIX(c);
      s = s1;
      s1 = NULL;
    }
    else
    {
      if (!c->Suffix)
      {
        #ifdef PPMD8_FREEZE_SUPPORT
        if (p->RestoreMethod > PPMD8_RESTORE_METHOD_FREEZE)
        {
          do { SetSuccessor(ps[--numPs], REF(c)); } while (numPs);
          RESET_TEXT(1);
          p->OrderFall = 1;
        }
        #endif
        return c;
      }
      c = SUFFIX(c);
      if (c->NumStats)
      {
        if ((s = STATS(c))->Symbol != p->FoundState->Symbol)
          do { s++; } while (s->Symbol != p->FoundState->Symbol);
        if (s->Freq < MAX_FREQ - 9)
        {
          s->Freq += 2;
          c->SummFreq += 2;
        }
      }
      else
      {
        s = ONE_STATE(c);
        s->Freq = (Byte)(s->Freq + (s->Freq < 32));
      }
    }
    if (SUCCESSOR(s))
      break;
    #ifdef PPMD8_FREEZE_SUPPORT
    ps[numPs++] = s;
    #endif
    SetSuccessor(s, upBranch);
    p->OrderFall++;
  }
  
  #ifdef PPMD8_FREEZE_SUPPORT
  if (p->RestoreMethod > PPMD8_RESTORE_METHOD_FREEZE)
  {
    c = CTX(SUCCESSOR(s));
    do { SetSuccessor(ps[--numPs], REF(c)); } while (numPs);
    RESET_TEXT(1);
    p->OrderFall = 1;
    return c;
  }
  else
  #endif
  if (SUCCESSOR(s) <= upBranch)
  {
    CTX_PTR successor;
    CPpmd_State *s2 = p->FoundState;
    p->FoundState = s;

    successor = CreateSuccessors(p, False, NULL, c);
    if (successor == NULL)
      SetSuccessor(s, 0);
    else
      SetSuccessor(s, REF(successor));
    p->FoundState = s2;
  }
  
  if (p->OrderFall == 1 && c1 == p->MaxContext)
  {
    SetSuccessor(p->FoundState, SUCCESSOR(s));
    p->Text--;
  }
  if (SUCCESSOR(s) == 0)
    return NULL;
  return CTX(SUCCESSOR(s));
}

static void UpdateModel(CPpmd8 *p)
{
  CPpmd_Void_Ref successor, fSuccessor = SUCCESSOR(p->FoundState);
  CTX_PTR c;
  unsigned s0, ns, fFreq = p->FoundState->Freq;
  Byte flag, fSymbol = p->FoundState->Symbol;
  CPpmd_State *s = NULL;
  
  if (p->FoundState->Freq < MAX_FREQ / 4 && p->MinContext->Suffix != 0)
  {
    c = SUFFIX(p->MinContext);
    
    if (c->NumStats == 0)
    {
      s = ONE_STATE(c);
      if (s->Freq < 32)
        s->Freq++;
    }
    else
    {
      s = STATS(c);
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
  
  c = p->MaxContext;
  if (p->OrderFall == 0 && fSuccessor)
  {
    CTX_PTR cs = CreateSuccessors(p, True, s, p->MinContext);
    if (cs == 0)
    {
      SetSuccessor(p->FoundState, 0);
      RESTORE_MODEL(c, CTX(fSuccessor));
    }
    else
    {
      SetSuccessor(p->FoundState, REF(cs));
      p->MaxContext = cs;
    }
    return;
  }
  
  *p->Text++ = p->FoundState->Symbol;
  successor = REF(p->Text);
  if (p->Text >= p->UnitsStart)
  {
    RESTORE_MODEL(c, CTX(fSuccessor)); /* check it */
    return;
  }
  
  if (!fSuccessor)
  {
    CTX_PTR cs = ReduceOrder(p, s, p->MinContext);
    if (cs == NULL)
    {
      RESTORE_MODEL(c, 0);
      return;
    }
    fSuccessor = REF(cs);
  }
  else if ((Byte *)Ppmd8_GetPtr(p, fSuccessor) < p->UnitsStart)
  {
    CTX_PTR cs = CreateSuccessors(p, False, s, p->MinContext);
    if (cs == NULL)
    {
      RESTORE_MODEL(c, 0);
      return;
    }
    fSuccessor = REF(cs);
  }
  
  if (--p->OrderFall == 0)
  {
    successor = fSuccessor;
    p->Text -= (p->MaxContext != p->MinContext);
  }
  #ifdef PPMD8_FREEZE_SUPPORT
  else if (p->RestoreMethod > PPMD8_RESTORE_METHOD_FREEZE)
  {
    successor = fSuccessor;
    RESET_TEXT(0);
    p->OrderFall = 0;
  }
  #endif
  
  s0 = p->MinContext->SummFreq - (ns = p->MinContext->NumStats) - fFreq;
  flag = (Byte)(0x08 * (fSymbol >= 0x40));
  
  for (; c != p->MinContext; c = SUFFIX(c))
  {
    unsigned ns1;
    UInt32 cf, sf;
    if ((ns1 = c->NumStats) != 0)
    {
      if ((ns1 & 1) != 0)
      {
        /* Expand for one UNIT */
        unsigned oldNU = (ns1 + 1) >> 1;
        unsigned i = U2I(oldNU);
        if (i != U2I(oldNU + 1))
        {
          void *ptr = AllocUnits(p, i + 1);
          void *oldPtr;
          if (!ptr)
          {
            RESTORE_MODEL(c, CTX(fSuccessor));
            return;
          }
          oldPtr = STATS(c);
          MyMem12Cpy(ptr, oldPtr, oldNU);
          InsertNode(p, oldPtr, i);
          c->Stats = STATS_REF(ptr);
        }
      }
      c->SummFreq = (UInt16)(c->SummFreq + (3 * ns1 + 1 < ns));
    }
    else
    {
      CPpmd_State *s2 = (CPpmd_State*)AllocUnits(p, 0);
      if (!s2)
      {
        RESTORE_MODEL(c, CTX(fSuccessor));
        return;
      }
      *s2 = *ONE_STATE(c);
      c->Stats = REF(s2);
      if (s2->Freq < MAX_FREQ / 4 - 1)
        s2->Freq <<= 1;
      else
        s2->Freq = MAX_FREQ - 4;
      c->SummFreq = (UInt16)(s2->Freq + p->InitEsc + (ns > 2));
    }
    cf = 2 * fFreq * (c->SummFreq + 6);
    sf = (UInt32)s0 + c->SummFreq;
    if (cf < 6 * sf)
    {
      cf = 1 + (cf > sf) + (cf >= 4 * sf);
      c->SummFreq += 4;
    }
    else
    {
      cf = 4 + (cf > 9 * sf) + (cf > 12 * sf) + (cf > 15 * sf);
      c->SummFreq = (UInt16)(c->SummFreq + cf);
    }
    {
      CPpmd_State *s2 = STATS(c) + ns1 + 1;
      SetSuccessor(s2, successor);
      s2->Symbol = fSymbol;
      s2->Freq = (Byte)cf;
      c->Flags |= flag;
      c->NumStats = (Byte)(ns1 + 1);
    }
  }
  p->MaxContext = p->MinContext = CTX(fSuccessor);
}
  
static void Rescale(CPpmd8 *p)
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
  adder = (p->OrderFall != 0
      #ifdef PPMD8_FREEZE_SUPPORT
      || p->RestoreMethod > PPMD8_RESTORE_METHOD_FREEZE
      #endif
      );
  s->Freq = (Byte)((s->Freq + adder) >> 1);
  sumFreq = s->Freq;
  
  i = p->MinContext->NumStats;
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
    p->MinContext->NumStats = (Byte)(p->MinContext->NumStats - i);
    if (p->MinContext->NumStats == 0)
    {
      CPpmd_State tmp = *stats;
      tmp.Freq = (Byte)((2 * tmp.Freq + escFreq - 1) / escFreq);
      if (tmp.Freq > MAX_FREQ / 3)
        tmp.Freq = MAX_FREQ / 3;
      InsertNode(p, stats, U2I((numStats + 2) >> 1));
      p->MinContext->Flags = (Byte)((p->MinContext->Flags & 0x10) + 0x08 * (tmp.Symbol >= 0x40));
      *(p->FoundState = ONE_STATE(p->MinContext)) = tmp;
      return;
    }
    n0 = (numStats + 2) >> 1;
    n1 = (p->MinContext->NumStats + 2) >> 1;
    if (n0 != n1)
      p->MinContext->Stats = STATS_REF(ShrinkUnits(p, stats, n0, n1));
    p->MinContext->Flags &= ~0x08;
    p->MinContext->Flags |= 0x08 * ((s = STATS(p->MinContext))->Symbol >= 0x40);
    i = p->MinContext->NumStats;
    do { p->MinContext->Flags |= 0x08*((++s)->Symbol >= 0x40); } while (--i);
  }
  p->MinContext->SummFreq = (UInt16)(sumFreq + escFreq - (escFreq >> 1));
  p->MinContext->Flags |= 0x4;
  p->FoundState = STATS(p->MinContext);
}

CPpmd_See *Ppmd8_MakeEscFreq(CPpmd8 *p, unsigned numMasked1, UInt32 *escFreq)
{
  CPpmd_See *see;
  if (p->MinContext->NumStats != 0xFF)
  {
    see = p->See[(unsigned)p->NS2Indx[(unsigned)p->MinContext->NumStats + 2] - 3] +
        (p->MinContext->SummFreq > 11 * ((unsigned)p->MinContext->NumStats + 1)) +
        2 * (unsigned)(2 * (unsigned)p->MinContext->NumStats <
        ((unsigned)SUFFIX(p->MinContext)->NumStats + numMasked1)) +
        p->MinContext->Flags;
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

static void NextContext(CPpmd8 *p)
{
  CTX_PTR c = CTX(SUCCESSOR(p->FoundState));
  if (p->OrderFall == 0 && (Byte *)c >= p->UnitsStart)
    p->MinContext = p->MaxContext = c;
  else
  {
    UpdateModel(p);
    p->MinContext = p->MaxContext;
  }
}

void Ppmd8_Update1(CPpmd8 *p)
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

void Ppmd8_Update1_0(CPpmd8 *p)
{
  p->PrevSuccess = (2 * p->FoundState->Freq >= p->MinContext->SummFreq);
  p->RunLength += p->PrevSuccess;
  p->MinContext->SummFreq += 4;
  if ((p->FoundState->Freq += 4) > MAX_FREQ)
    Rescale(p);
  NextContext(p);
}

void Ppmd8_UpdateBin(CPpmd8 *p)
{
  p->FoundState->Freq = (Byte)(p->FoundState->Freq + (p->FoundState->Freq < 196));
  p->PrevSuccess = 1;
  p->RunLength++;
  NextContext(p);
}

void Ppmd8_Update2(CPpmd8 *p)
{
  p->MinContext->SummFreq += 4;
  if ((p->FoundState->Freq += 4) > MAX_FREQ)
    Rescale(p);
  p->RunLength = p->InitRL;
  UpdateModel(p);
  p->MinContext = p->MaxContext;
}

/* Ppmd8Dec.c -- PPMdI Decoder
2010-04-16 : Igor Pavlov : Public domain
This code is based on:
  PPMd var.I (2002): Dmitry Shkarin : Public domain
  Carryless rangecoder (1999): Dmitry Subbotin : Public domain */

Bool Ppmd8_RangeDec_Init(CPpmd8 *p)
{
  unsigned i;
  p->Low = 0;
  p->Range = 0xFFFFFFFF;
  p->Code = 0;
  for (i = 0; i < 4; i++)
    p->Code = (p->Code << 8) | p->Stream.In->Read(p->Stream.In);
  return (p->Code < 0xFFFFFFFF);
}

static UInt32 RangeDec_GetThreshold(CPpmd8 *p, UInt32 total)
{
  return p->Code / (p->Range /= total);
}

static void RangeDec_Decode(CPpmd8 *p, UInt32 start, UInt32 size)
{
  start *= p->Range;
  p->Low += start;
  p->Code -= start;
  p->Range *= size;

  while ((p->Low ^ (p->Low + p->Range)) < kTop ||
      (p->Range < kBot && ((p->Range = (0 - p->Low) & (kBot - 1)), 1)))
  {
    p->Code = (p->Code << 8) | p->Stream.In->Read(p->Stream.In);
    p->Range <<= 8;
    p->Low <<= 8;
  }
}

#define MASK(sym) ((signed char *)charMask)[sym]

int Ppmd8_DecodeSymbol(CPpmd8 *p)
{
  size_t charMask[256 / sizeof(size_t)];
  if (p->MinContext->NumStats != 0)
  {
    CPpmd_State *s = Ppmd8_GetStats(p, p->MinContext);
    unsigned i;
    UInt32 count, hiCnt;
    if ((count = RangeDec_GetThreshold(p, p->MinContext->SummFreq)) < (hiCnt = s->Freq))
    {
      Byte symbol;
      RangeDec_Decode(p, 0, s->Freq);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd8_Update1_0(p);
      return symbol;
    }
    p->PrevSuccess = 0;
    i = p->MinContext->NumStats;
    do
    {
      if ((hiCnt += (++s)->Freq) > count)
      {
        Byte symbol;
        RangeDec_Decode(p, hiCnt - s->Freq, s->Freq);
        p->FoundState = s;
        symbol = s->Symbol;
        Ppmd8_Update1(p);
        return symbol;
      }
    }
    while (--i);
    if (count >= p->MinContext->SummFreq)
      return -2;
    RangeDec_Decode(p, hiCnt, p->MinContext->SummFreq - hiCnt);
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats;
    do { MASK((--s)->Symbol) = 0; } while (--i);
  }
  else
  {
    UInt16 *prob = Ppmd8_GetBinSumm(p);
    if (((p->Code / (p->Range >>= 14)) < *prob))
    {
      Byte symbol;
      RangeDec_Decode(p, 0, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      symbol = (p->FoundState = Ppmd8Context_OneState(p->MinContext))->Symbol;
      Ppmd8_UpdateBin(p);
      return symbol;
    }
    RangeDec_Decode(p, *prob, (1 << 14) - *prob);
    *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
    p->InitEsc = PPMD8_kExpEscape[*prob >> 10];
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(Ppmd8Context_OneState(p->MinContext)->Symbol) = 0;
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
      p->MinContext = Ppmd8_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    hiCnt = 0;
    s = Ppmd8_GetStats(p, p->MinContext);
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
    
    see = Ppmd8_MakeEscFreq(p, numMasked, &freqSum);
    freqSum += hiCnt;
    count = RangeDec_GetThreshold(p, freqSum);
    
    if (count < hiCnt)
    {
      Byte symbol;
      CPpmd_State **pps = ps;
      for (hiCnt = 0; (hiCnt += (*pps)->Freq) <= count; pps++);
      s = *pps;
      RangeDec_Decode(p, hiCnt - s->Freq, s->Freq);
      Ppmd_See_Update(see);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd8_Update2(p);
      return symbol;
    }
    if (count >= freqSum)
      return -2;
    RangeDec_Decode(p, hiCnt, freqSum - hiCnt);
    see->Summ = (UInt16)(see->Summ + freqSum);
    do { MASK(ps[--i]->Symbol) = 0; } while (i != 0);
  }
}

/* H->I changes:
  NS2Indx
  GlewCount, and Glue method
  BinSum
  See / EscFreq
  CreateSuccessors updates more suffix contexts
  UpdateModel consts.
  PrevSuccess Update
*/

const IPpmd8 __archive_ppmd8_functions =
{
  &Ppmd8_Construct,
  &Ppmd8_Alloc,
  &Ppmd8_Free,
  &Ppmd8_Init,
  &Ppmd8_RangeDec_Init,
  &Ppmd8_DecodeSymbol,
};
