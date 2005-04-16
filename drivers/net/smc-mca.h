/*
 * djweis weisd3458@uni.edu
 * most of this file was taken from ps2esdi.h
 */

struct {
  unsigned int base_addr;
} addr_table[] = {
    { 0x0800 },
    { 0x1800 },
    { 0x2800 },
    { 0x3800 },
    { 0x4800 },
    { 0x5800 },
    { 0x6800 },
    { 0x7800 },
    { 0x8800 },
    { 0x9800 },
    { 0xa800 },
    { 0xb800 },
    { 0xc800 },
    { 0xd800 },
    { 0xe800 },
    { 0xf800 }
};

#define MEM_MASK 64

struct {
  unsigned char mem_index;
  unsigned long mem_start;
  unsigned char num_pages;
} mem_table[] = {
    { 16, 0x0c0000, 40 },
    { 18, 0x0c4000, 40 },
    { 20, 0x0c8000, 40 },
    { 22, 0x0cc000, 40 },
    { 24, 0x0d0000, 40 },
    { 26, 0x0d4000, 40 },
    { 28, 0x0d8000, 40 },
    { 30, 0x0dc000, 40 },
    {144, 0xfc0000, 40 },
    {148, 0xfc8000, 40 },
    {154, 0xfd0000, 40 },
    {156, 0xfd8000, 40 },
    {  0, 0x0c0000, 20 },
    {  1, 0x0c2000, 20 },
    {  2, 0x0c4000, 20 },
    {  3, 0x0c6000, 20 }
};

#define IRQ_MASK 243
struct {
   unsigned char new_irq;
   unsigned char old_irq;
} irq_table[] = {
   {  3,  3 },
   {  4,  4 },
   { 10, 10 },
   { 14, 15 }
};
