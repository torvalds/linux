//===- Math.h - PBQP Vector and Matrix classes ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQP_MATH_H
#define LLVM_CODEGEN_PBQP_MATH_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>

namespace llvm {
namespace PBQP {

using PBQPNum = float;

/// PBQP Vector class.
class Vector {
  friend hash_code hash_value(const Vector &);

public:
  /// Construct a PBQP vector of the given size.
  explicit Vector(unsigned Length)
    : Length(Length), Data(llvm::make_unique<PBQPNum []>(Length)) {}

  /// Construct a PBQP vector with initializer.
  Vector(unsigned Length, PBQPNum InitVal)
    : Length(Length), Data(llvm::make_unique<PBQPNum []>(Length)) {
    std::fill(Data.get(), Data.get() + Length, InitVal);
  }

  /// Copy construct a PBQP vector.
  Vector(const Vector &V)
    : Length(V.Length), Data(llvm::make_unique<PBQPNum []>(Length)) {
    std::copy(V.Data.get(), V.Data.get() + Length, Data.get());
  }

  /// Move construct a PBQP vector.
  Vector(Vector &&V)
    : Length(V.Length), Data(std::move(V.Data)) {
    V.Length = 0;
  }

  /// Comparison operator.
  bool operator==(const Vector &V) const {
    assert(Length != 0 && Data && "Invalid vector");
    if (Length != V.Length)
      return false;
    return std::equal(Data.get(), Data.get() + Length, V.Data.get());
  }

  /// Return the length of the vector
  unsigned getLength() const {
    assert(Length != 0 && Data && "Invalid vector");
    return Length;
  }

  /// Element access.
  PBQPNum& operator[](unsigned Index) {
    assert(Length != 0 && Data && "Invalid vector");
    assert(Index < Length && "Vector element access out of bounds.");
    return Data[Index];
  }

  /// Const element access.
  const PBQPNum& operator[](unsigned Index) const {
    assert(Length != 0 && Data && "Invalid vector");
    assert(Index < Length && "Vector element access out of bounds.");
    return Data[Index];
  }

  /// Add another vector to this one.
  Vector& operator+=(const Vector &V) {
    assert(Length != 0 && Data && "Invalid vector");
    assert(Length == V.Length && "Vector length mismatch.");
    std::transform(Data.get(), Data.get() + Length, V.Data.get(), Data.get(),
                   std::plus<PBQPNum>());
    return *this;
  }

  /// Returns the index of the minimum value in this vector
  unsigned minIndex() const {
    assert(Length != 0 && Data && "Invalid vector");
    return std::min_element(Data.get(), Data.get() + Length) - Data.get();
  }

private:
  unsigned Length;
  std::unique_ptr<PBQPNum []> Data;
};

/// Return a hash_value for the given vector.
inline hash_code hash_value(const Vector &V) {
  unsigned *VBegin = reinterpret_cast<unsigned*>(V.Data.get());
  unsigned *VEnd = reinterpret_cast<unsigned*>(V.Data.get() + V.Length);
  return hash_combine(V.Length, hash_combine_range(VBegin, VEnd));
}

/// Output a textual representation of the given vector on the given
///        output stream.
template <typename OStream>
OStream& operator<<(OStream &OS, const Vector &V) {
  assert((V.getLength() != 0) && "Zero-length vector badness.");

  OS << "[ " << V[0];
  for (unsigned i = 1; i < V.getLength(); ++i)
    OS << ", " << V[i];
  OS << " ]";

  return OS;
}

/// PBQP Matrix class
class Matrix {
private:
  friend hash_code hash_value(const Matrix &);

public:
  /// Construct a PBQP Matrix with the given dimensions.
  Matrix(unsigned Rows, unsigned Cols) :
    Rows(Rows), Cols(Cols), Data(llvm::make_unique<PBQPNum []>(Rows * Cols)) {
  }

  /// Construct a PBQP Matrix with the given dimensions and initial
  /// value.
  Matrix(unsigned Rows, unsigned Cols, PBQPNum InitVal)
    : Rows(Rows), Cols(Cols),
      Data(llvm::make_unique<PBQPNum []>(Rows * Cols)) {
    std::fill(Data.get(), Data.get() + (Rows * Cols), InitVal);
  }

  /// Copy construct a PBQP matrix.
  Matrix(const Matrix &M)
    : Rows(M.Rows), Cols(M.Cols),
      Data(llvm::make_unique<PBQPNum []>(Rows * Cols)) {
    std::copy(M.Data.get(), M.Data.get() + (Rows * Cols), Data.get());
  }

  /// Move construct a PBQP matrix.
  Matrix(Matrix &&M)
    : Rows(M.Rows), Cols(M.Cols), Data(std::move(M.Data)) {
    M.Rows = M.Cols = 0;
  }

  /// Comparison operator.
  bool operator==(const Matrix &M) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    if (Rows != M.Rows || Cols != M.Cols)
      return false;
    return std::equal(Data.get(), Data.get() + (Rows * Cols), M.Data.get());
  }

  /// Return the number of rows in this matrix.
  unsigned getRows() const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    return Rows;
  }

  /// Return the number of cols in this matrix.
  unsigned getCols() const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    return Cols;
  }

  /// Matrix element access.
  PBQPNum* operator[](unsigned R) {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    assert(R < Rows && "Row out of bounds.");
    return Data.get() + (R * Cols);
  }

  /// Matrix element access.
  const PBQPNum* operator[](unsigned R) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    assert(R < Rows && "Row out of bounds.");
    return Data.get() + (R * Cols);
  }

  /// Returns the given row as a vector.
  Vector getRowAsVector(unsigned R) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Vector V(Cols);
    for (unsigned C = 0; C < Cols; ++C)
      V[C] = (*this)[R][C];
    return V;
  }

  /// Returns the given column as a vector.
  Vector getColAsVector(unsigned C) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Vector V(Rows);
    for (unsigned R = 0; R < Rows; ++R)
      V[R] = (*this)[R][C];
    return V;
  }

  /// Matrix transpose.
  Matrix transpose() const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Matrix M(Cols, Rows);
    for (unsigned r = 0; r < Rows; ++r)
      for (unsigned c = 0; c < Cols; ++c)
        M[c][r] = (*this)[r][c];
    return M;
  }

  /// Add the given matrix to this one.
  Matrix& operator+=(const Matrix &M) {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    assert(Rows == M.Rows && Cols == M.Cols &&
           "Matrix dimensions mismatch.");
    std::transform(Data.get(), Data.get() + (Rows * Cols), M.Data.get(),
                   Data.get(), std::plus<PBQPNum>());
    return *this;
  }

  Matrix operator+(const Matrix &M) {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Matrix Tmp(*this);
    Tmp += M;
    return Tmp;
  }

private:
  unsigned Rows, Cols;
  std::unique_ptr<PBQPNum []> Data;
};

/// Return a hash_code for the given matrix.
inline hash_code hash_value(const Matrix &M) {
  unsigned *MBegin = reinterpret_cast<unsigned*>(M.Data.get());
  unsigned *MEnd =
    reinterpret_cast<unsigned*>(M.Data.get() + (M.Rows * M.Cols));
  return hash_combine(M.Rows, M.Cols, hash_combine_range(MBegin, MEnd));
}

/// Output a textual representation of the given matrix on the given
///        output stream.
template <typename OStream>
OStream& operator<<(OStream &OS, const Matrix &M) {
  assert((M.getRows() != 0) && "Zero-row matrix badness.");
  for (unsigned i = 0; i < M.getRows(); ++i)
    OS << M.getRowAsVector(i) << "\n";
  return OS;
}

template <typename Metadata>
class MDVector : public Vector {
public:
  MDVector(const Vector &v) : Vector(v), md(*this) {}
  MDVector(Vector &&v) : Vector(std::move(v)), md(*this) { }

  const Metadata& getMetadata() const { return md; }

private:
  Metadata md;
};

template <typename Metadata>
inline hash_code hash_value(const MDVector<Metadata> &V) {
  return hash_value(static_cast<const Vector&>(V));
}

template <typename Metadata>
class MDMatrix : public Matrix {
public:
  MDMatrix(const Matrix &m) : Matrix(m), md(*this) {}
  MDMatrix(Matrix &&m) : Matrix(std::move(m)), md(*this) { }

  const Metadata& getMetadata() const { return md; }

private:
  Metadata md;
};

template <typename Metadata>
inline hash_code hash_value(const MDMatrix<Metadata> &M) {
  return hash_value(static_cast<const Matrix&>(M));
}

} // end namespace PBQP
} // end namespace llvm

#endif // LLVM_CODEGEN_PBQP_MATH_H
