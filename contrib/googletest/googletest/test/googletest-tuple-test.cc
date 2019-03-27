// Copyright 2007, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#include "gtest/internal/gtest-tuple.h"
#include <utility>
#include "gtest/gtest.h"

namespace {

using ::std::tr1::get;
using ::std::tr1::make_tuple;
using ::std::tr1::tuple;
using ::std::tr1::tuple_element;
using ::std::tr1::tuple_size;
using ::testing::StaticAssertTypeEq;

// Tests that tuple_element<K, tuple<T0, T1, ..., TN> >::type returns TK.
TEST(tuple_element_Test, ReturnsElementType) {
  StaticAssertTypeEq<int, tuple_element<0, tuple<int, char> >::type>();
  StaticAssertTypeEq<int&, tuple_element<1, tuple<double, int&> >::type>();
  StaticAssertTypeEq<bool, tuple_element<2, tuple<double, int, bool> >::type>();
}

// Tests that tuple_size<T>::value gives the number of fields in tuple
// type T.
TEST(tuple_size_Test, ReturnsNumberOfFields) {
  EXPECT_EQ(0, +tuple_size<tuple<> >::value);
  EXPECT_EQ(1, +tuple_size<tuple<void*> >::value);
  EXPECT_EQ(1, +tuple_size<tuple<char> >::value);
  EXPECT_EQ(1, +(tuple_size<tuple<tuple<int, double> > >::value));
  EXPECT_EQ(2, +(tuple_size<tuple<int&, const char> >::value));
  EXPECT_EQ(3, +(tuple_size<tuple<char*, void, const bool&> >::value));
}

// Tests comparing a tuple with itself.
TEST(ComparisonTest, ComparesWithSelf) {
  const tuple<int, char, bool> a(5, 'a', false);

  EXPECT_TRUE(a == a);
  EXPECT_FALSE(a != a);
}

// Tests comparing two tuples with the same value.
TEST(ComparisonTest, ComparesEqualTuples) {
  const tuple<int, bool> a(5, true), b(5, true);

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
}

// Tests comparing two different tuples that have no reference fields.
TEST(ComparisonTest, ComparesUnequalTuplesWithoutReferenceFields) {
  typedef tuple<const int, char> FooTuple;

  const FooTuple a(0, 'x');
  const FooTuple b(1, 'a');

  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a == b);

  const FooTuple c(1, 'b');

  EXPECT_TRUE(b != c);
  EXPECT_FALSE(b == c);
}

// Tests comparing two different tuples that have reference fields.
TEST(ComparisonTest, ComparesUnequalTuplesWithReferenceFields) {
  typedef tuple<int&, const char&> FooTuple;

  int i = 5;
  const char ch = 'a';
  const FooTuple a(i, ch);

  int j = 6;
  const FooTuple b(j, ch);

  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a == b);

  j = 5;
  const char ch2 = 'b';
  const FooTuple c(j, ch2);

  EXPECT_TRUE(b != c);
  EXPECT_FALSE(b == c);
}

// Tests that a tuple field with a reference type is an alias of the
// variable it's supposed to reference.
TEST(ReferenceFieldTest, IsAliasOfReferencedVariable) {
  int n = 0;
  tuple<bool, int&> t(true, n);

  n = 1;
  EXPECT_EQ(n, get<1>(t))
      << "Changing a underlying variable should update the reference field.";

  // Makes sure that the implementation doesn't do anything funny with
  // the & operator for the return type of get<>().
  EXPECT_EQ(&n, &(get<1>(t)))
      << "The address of a reference field should equal the address of "
      << "the underlying variable.";

  get<1>(t) = 2;
  EXPECT_EQ(2, n)
      << "Changing a reference field should update the underlying variable.";
}

// Tests that tuple's default constructor default initializes each field.
// This test needs to compile without generating warnings.
TEST(TupleConstructorTest, DefaultConstructorDefaultInitializesEachField) {
  // The TR1 report requires that tuple's default constructor default
  // initializes each field, even if it's a primitive type.  If the
  // implementation forgets to do this, this test will catch it by
  // generating warnings about using uninitialized variables (assuming
  // a decent compiler).

  tuple<> empty;

  tuple<int> a1, b1;
  b1 = a1;
  EXPECT_EQ(0, get<0>(b1));

  tuple<int, double> a2, b2;
  b2 = a2;
  EXPECT_EQ(0, get<0>(b2));
  EXPECT_EQ(0.0, get<1>(b2));

  tuple<double, char, bool*> a3, b3;
  b3 = a3;
  EXPECT_EQ(0.0, get<0>(b3));
  EXPECT_EQ('\0', get<1>(b3));
  EXPECT_TRUE(get<2>(b3) == NULL);

  tuple<int, int, int, int, int, int, int, int, int, int> a10, b10;
  b10 = a10;
  EXPECT_EQ(0, get<0>(b10));
  EXPECT_EQ(0, get<1>(b10));
  EXPECT_EQ(0, get<2>(b10));
  EXPECT_EQ(0, get<3>(b10));
  EXPECT_EQ(0, get<4>(b10));
  EXPECT_EQ(0, get<5>(b10));
  EXPECT_EQ(0, get<6>(b10));
  EXPECT_EQ(0, get<7>(b10));
  EXPECT_EQ(0, get<8>(b10));
  EXPECT_EQ(0, get<9>(b10));
}

// Tests constructing a tuple from its fields.
TEST(TupleConstructorTest, ConstructsFromFields) {
  int n = 1;
  // Reference field.
  tuple<int&> a(n);
  EXPECT_EQ(&n, &(get<0>(a)));

  // Non-reference fields.
  tuple<int, char> b(5, 'a');
  EXPECT_EQ(5, get<0>(b));
  EXPECT_EQ('a', get<1>(b));

  // Const reference field.
  const int m = 2;
  tuple<bool, const int&> c(true, m);
  EXPECT_TRUE(get<0>(c));
  EXPECT_EQ(&m, &(get<1>(c)));
}

// Tests tuple's copy constructor.
TEST(TupleConstructorTest, CopyConstructor) {
  tuple<double, bool> a(0.0, true);
  tuple<double, bool> b(a);

  EXPECT_DOUBLE_EQ(0.0, get<0>(b));
  EXPECT_TRUE(get<1>(b));
}

// Tests constructing a tuple from another tuple that has a compatible
// but different type.
TEST(TupleConstructorTest, ConstructsFromDifferentTupleType) {
  tuple<int, int, char> a(0, 1, 'a');
  tuple<double, long, int> b(a);

  EXPECT_DOUBLE_EQ(0.0, get<0>(b));
  EXPECT_EQ(1, get<1>(b));
  EXPECT_EQ('a', get<2>(b));
}

// Tests constructing a 2-tuple from an std::pair.
TEST(TupleConstructorTest, ConstructsFromPair) {
  ::std::pair<int, char> a(1, 'a');
  tuple<int, char> b(a);
  tuple<int, const char&> c(a);
}

// Tests assigning a tuple to another tuple with the same type.
TEST(TupleAssignmentTest, AssignsToSameTupleType) {
  const tuple<int, long> a(5, 7L);
  tuple<int, long> b;
  b = a;
  EXPECT_EQ(5, get<0>(b));
  EXPECT_EQ(7L, get<1>(b));
}

// Tests assigning a tuple to another tuple with a different but
// compatible type.
TEST(TupleAssignmentTest, AssignsToDifferentTupleType) {
  const tuple<int, long, bool> a(1, 7L, true);
  tuple<long, int, bool> b;
  b = a;
  EXPECT_EQ(1L, get<0>(b));
  EXPECT_EQ(7, get<1>(b));
  EXPECT_TRUE(get<2>(b));
}

// Tests assigning an std::pair to a 2-tuple.
TEST(TupleAssignmentTest, AssignsFromPair) {
  const ::std::pair<int, bool> a(5, true);
  tuple<int, bool> b;
  b = a;
  EXPECT_EQ(5, get<0>(b));
  EXPECT_TRUE(get<1>(b));

  tuple<long, bool> c;
  c = a;
  EXPECT_EQ(5L, get<0>(c));
  EXPECT_TRUE(get<1>(c));
}

// A fixture for testing big tuples.
class BigTupleTest : public testing::Test {
 protected:
  typedef tuple<int, int, int, int, int, int, int, int, int, int> BigTuple;

  BigTupleTest() :
      a_(1, 0, 0, 0, 0, 0, 0, 0, 0, 2),
      b_(1, 0, 0, 0, 0, 0, 0, 0, 0, 3) {}

  BigTuple a_, b_;
};

// Tests constructing big tuples.
TEST_F(BigTupleTest, Construction) {
  BigTuple a;
  BigTuple b(b_);
}

// Tests that get<N>(t) returns the N-th (0-based) field of tuple t.
TEST_F(BigTupleTest, get) {
  EXPECT_EQ(1, get<0>(a_));
  EXPECT_EQ(2, get<9>(a_));

  // Tests that get() works on a const tuple too.
  const BigTuple a(a_);
  EXPECT_EQ(1, get<0>(a));
  EXPECT_EQ(2, get<9>(a));
}

// Tests comparing big tuples.
TEST_F(BigTupleTest, Comparisons) {
  EXPECT_TRUE(a_ == a_);
  EXPECT_FALSE(a_ != a_);

  EXPECT_TRUE(a_ != b_);
  EXPECT_FALSE(a_ == b_);
}

TEST(MakeTupleTest, WorksForScalarTypes) {
  tuple<bool, int> a;
  a = make_tuple(true, 5);
  EXPECT_TRUE(get<0>(a));
  EXPECT_EQ(5, get<1>(a));

  tuple<char, int, long> b;
  b = make_tuple('a', 'b', 5);
  EXPECT_EQ('a', get<0>(b));
  EXPECT_EQ('b', get<1>(b));
  EXPECT_EQ(5, get<2>(b));
}

TEST(MakeTupleTest, WorksForPointers) {
  int a[] = { 1, 2, 3, 4 };
  const char* const str = "hi";
  int* const p = a;

  tuple<const char*, int*> t;
  t = make_tuple(str, p);
  EXPECT_EQ(str, get<0>(t));
  EXPECT_EQ(p, get<1>(t));
}

}  // namespace
